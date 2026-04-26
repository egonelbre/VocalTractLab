#include "SpectrumPanel.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioEngine.h"
#include "acoustics/TlModel.h"
#include "acoustics/Tube.h"
#include "anatomy/VocalTract.h"
#include "core/Constants.h"
#include "dsp/Dsp.h"
#include "dsp/Signal.h"
#include "imgui.h"

namespace live {

namespace {

// Length of the VTTF spectrum. 4096 bins at 48 kHz gives ~12 Hz resolution,
// plenty to render smooth formant peaks across a 0–8 kHz axis. Matches the
// SPECTRUM_LENGTH the wxWidgets gui uses for the same view.
constexpr int VTTF_LEN_EXPONENT = 12;
constexpr int VTTF_LEN = 1 << VTTF_LEN_EXPONENT;
constexpr int MAX_FORMANTS = 4;

// Piano keyboard strip rendered along the bottom of the panel as a
// frequency reference. Drawn below the spectrum plotting area; key
// positions use the same xForFreq mapping the curves use, so notes
// land directly under the harmonic stems they correspond to. Linear
// Hz means low notes squeeze together at the left and high notes
// spread out; log Hz makes the keys nearly uniform width — pitch is
// logarithmic in frequency, so a log axis is the natural fit.
void drawPianoKeyboard(ImDrawList* dl, ImVec2 kbdMin, ImVec2 kbdMax,
                       double fMin_Hz, double fMax_Hz, FreqScale scale) {
  const float kbdW = kbdMax.x - kbdMin.x;
  const float kbdH = kbdMax.y - kbdMin.y;
  if (kbdW < 1.0f || kbdH < 1.0f) return;

  // Conventional piano colors so the strip reads correctly under both
  // light and dark themes.
  const ImU32 colWhite = IM_COL32(225, 225, 220, 255);
  const ImU32 colBlack = IM_COL32(25, 25, 25, 255);
  const ImU32 colKeyBorder = IM_COL32(70, 70, 70, 255);
  const ImU32 colKeyText = IM_COL32(60, 60, 60, 255);

  const double logFmin = std::log(fMin_Hz > 0.0 ? fMin_Hz : 1.0);
  const double logFmax = std::log(fMax_Hz);
  const double logRange = logFmax - logFmin;
  auto xForFreq = [&](double f) {
    if (scale == FreqScale::Log) {
      double lf = std::log(f > 1.0 ? f : 1.0);
      return kbdMin.x + kbdW * (float)((lf - logFmin) / logRange);
    }
    return kbdMin.x +
           kbdW * (float)((f - fMin_Hz) / (fMax_Hz - fMin_Hz));
  };
  // MIDI -> Hz, MIDI 69 = A4 = 440 Hz.
  auto noteFreq = [](int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
  };
  // Black keys are at scale degrees C#, D#, F#, G#, A#.
  auto isBlack = [](int midi) {
    int n = ((midi % 12) + 12) % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
  };

  // Background fill — white keys are drawn via the gaps between black
  // keys plus this base layer, with vertical separators.
  dl->AddRectFilled(kbdMin, kbdMax, colWhite);

  // MIDI 12 = C0 (≈16.35 Hz); MIDI 119 = B8 (≈7902 Hz). Covers the
  // 0–8 kHz spectrum axis with one octave of headroom either side.
  const int firstMidi = 12;
  const int lastMidi = 119;

  // White-key separators + C labels.
  for (int m = firstMidi; m <= lastMidi; ++m) {
    if (isBlack(m)) continue;
    double f = noteFreq(m);
    // Right-edge boundary = midpoint to the next white key in frequency.
    int nextWhite = m + 1;
    while (nextWhite <= lastMidi && isBlack(nextWhite)) ++nextWhite;
    if (nextWhite > lastMidi) continue;
    double fNext = noteFreq(nextWhite);
    float xRight = xForFreq((f + fNext) / 2.0);
    if (xRight < kbdMin.x || xRight > kbdMax.x) continue;
    dl->AddLine(ImVec2(xRight, kbdMin.y), ImVec2(xRight, kbdMax.y),
                colKeyBorder, 1.0f);

    if (m % 12 == 0) {
      // Left edge of this white key for label placement.
      int prevWhite = m - 1;
      while (prevWhite >= firstMidi && isBlack(prevWhite)) --prevWhite;
      double fPrev = (prevWhite >= firstMidi) ? noteFreq(prevWhite) : f;
      float xLeft = xForFreq((f + fPrev) / 2.0);
      if (xLeft < kbdMin.x) xLeft = kbdMin.x;
      // Skip the label if the key is too narrow to read.
      if (xRight - xLeft >= 12.0f) {
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "C%d", m / 12 - 1);
        dl->AddText(ImVec2(xLeft + 2.0f, kbdMin.y + kbdH - 12.0f),
                    colKeyText, lbl);
      }
    }
  }

  // Black keys on top of the white key band, occupying the upper 60 %
  // of the strip height, centered between adjacent semitones.
  const float blackBottom = kbdMin.y + kbdH * 0.60f;
  for (int m = firstMidi; m <= lastMidi; ++m) {
    if (!isBlack(m)) continue;
    double f = noteFreq(m);
    double fPrev = noteFreq(m - 1);
    double fNext = noteFreq(m + 1);
    float xLeft = xForFreq((f + fPrev) / 2.0);
    float xRight = xForFreq((f + fNext) / 2.0);
    if (xRight < kbdMin.x || xLeft > kbdMax.x) continue;
    if (xLeft < kbdMin.x) xLeft = kbdMin.x;
    if (xRight > kbdMax.x) xRight = kbdMax.x;
    dl->AddRectFilled(ImVec2(xLeft, kbdMin.y),
                      ImVec2(xRight, blackBottom), colBlack);
  }
}

void drawSpectrum(ImDrawList* dl, AudioHistory& history, ComplexSignal& fftBuf,
                  VocalTract* uiTract, double f0_Hz, FreqScale scale,
                  ImVec2 canvasMin, ImVec2 canvasMaxFull) {
  // Reserve a strip at the bottom for the piano keyboard. Spectrum
  // curves, axis labels, and stems all use canvasMax (shrunk) so they
  // never overlap the keyboard band.
  const float kbdHeight = 32.0f;
  ImVec2 canvasMax = canvasMaxFull;
  if (canvasMaxFull.y - canvasMin.y > kbdHeight + 40.0f) {
    canvasMax.y = canvasMaxFull.y - kbdHeight;
  }
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW <= 1.0f || canvasH <= 1.0f) return;

  // Theme-aware: read colors back from the active ImGui style every frame
  // so light/dark/classic switches just work.
  const ImU32 colBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Text, 0.22f);
  const ImU32 colCurve = ImGui::GetColorU32(ImGuiCol_PlotLines);
  const ImU32 colVttf = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
  const ImU32 colFundamental = ImGui::GetColorU32(ImGuiCol_CheckMark);
  const ImU32 colHarmonic = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.55f);
  const ImU32 colFormant = ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 colBorder = ImGui::GetColorU32(ImGuiCol_Border);
  dl->AddRectFilled(canvasMin, canvasMax, colBg);

  // Log mode needs a non-zero lower bound (log(0) = -inf). 50 Hz sits
  // below typical singing F0 (~80 Hz) and below the lowest piano A0
  // (~27.5 Hz still falls outside, but A0 is below the human voice
  // anyway and would just be an off-scale stub).
  const bool isLog = (scale == FreqScale::Log);
  const double fMin_Hz = isLog ? 50.0 : 0.0;
  const double fMax_Hz = 8000.0;
  const double dbMin = -90.0;
  const double dbMax = 0.0;
  const double freqStep_Hz = (double)AUDIO_SAMPLING_RATE_HZ / (double)FFT_LEN;
  const double logFmin = std::log(isLog ? fMin_Hz : 1.0);
  const double logFmax = std::log(fMax_Hz);
  const double logRange = logFmax - logFmin;

  auto xForFreq = [&](double f) {
    if (isLog) {
      double lf = std::log(f > 1.0 ? f : 1.0);
      return canvasMin.x + canvasW * (float)((lf - logFmin) / logRange);
    }
    return canvasMin.x +
           canvasW * (float)((f - fMin_Hz) / (fMax_Hz - fMin_Hz));
  };
  // Inverse: pixel column -> Hz. Curve sampling loops use this so a
  // single per-pixel step covers an even slice of the displayed axis,
  // not just of frequency.
  auto freqForPx = [&](int px) {
    double t = (double)px / (double)canvasW;
    if (isLog) return std::exp(logFmin + logRange * t);
    return fMin_Hz + (fMax_Hz - fMin_Hz) * t;
  };
  auto yForDb = [&](double db) {
    if (db < dbMin) db = dbMin;
    if (db > dbMax) db = dbMax;
    return canvasMax.y -
           canvasH * (float)((db - dbMin) / (dbMax - dbMin));
  };

  // Frequency grid + axis labels. Linear mode uses kHz multiples; log
  // mode uses the standard "audio decade" set so each gridline lands at
  // a familiar frequency rather than at log(N).
  if (isLog) {
    static const double gridFreqs[] = {100, 200, 500, 1000, 2000, 5000};
    static const char* gridLabels[] = {"100", "200", "500", "1k", "2k", "5k"};
    for (int i = 0; i < (int)(sizeof(gridFreqs) / sizeof(gridFreqs[0])); ++i) {
      double f = gridFreqs[i];
      if (f < fMin_Hz || f > fMax_Hz) continue;
      float x = xForFreq(f);
      dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colGrid, 1.0f);
      dl->AddText(ImVec2(x + 2.0f, canvasMax.y - 14.0f), colText, gridLabels[i]);
    }
  } else {
    for (int khz = 1; khz < (int)(fMax_Hz / 1000.0); ++khz) {
      float x = xForFreq((double)khz * 1000.0);
      dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colGrid, 1.0f);
      char label[16];
      std::snprintf(label, sizeof(label), "%dk", khz);
      dl->AddText(ImVec2(x + 2.0f, canvasMax.y - 14.0f), colText, label);
    }
  }
  for (double db = dbMin + 20.0; db < dbMax; db += 20.0) {
    float y = yForDb(db);
    dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%d dB", (int)db);
    dl->AddText(ImVec2(canvasMin.x + 2.0f, y - 14.0f), colText, label);
  }

  // ----- Vocal tract transfer function + formants ---------------------------
  // Reuse a single TlModel across frames so its prevTube/prevOptions cache
  // skips redundant matrix math when the tract hasn't changed.
  static TlModel s_tlModel;
  static ComplexSignal s_vttf(VTTF_LEN);
  bool haveVttf = false;
  double formantFreq[MAX_FORMANTS] = {};
  double formantBw[MAX_FORMANTS] = {};
  int numFormants = 0;
  bool isClosure = false;

  if (uiTract != nullptr) {
    // Capture geometry from the UI tract (already calculateAll()'d by the
    // 2D panel earlier in the frame). Closing the glottis sections matches
    // the FLOW_SOURCE_TF convention used by the wxWidgets spectrum view.
    uiTract->getTube(&s_tlModel.tube);
    s_tlModel.tube.resetGlottisSections(0.0);
    s_tlModel.getSpectrum(TlModel::FLOW_SOURCE_TF, &s_vttf, VTTF_LEN,
                          Tube::FIRST_PHARYNX_SECTION);
    haveVttf = true;

    bool frictionNoise = false;
    bool isNasal = false;
    s_tlModel.getFormants(formantFreq, formantBw, numFormants, MAX_FORMANTS,
                          frictionNoise, isClosure, isNasal);
  }

  // ----- Audio FFT ----------------------------------------------------------
  static std::array<float, FFT_LEN> samples;
  history.copyLatest(samples.data(), FFT_LEN);

  fftBuf.reset(FFT_LEN);
  for (int i = 0; i < FFT_LEN; ++i) {
    double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (double)(FFT_LEN - 1));
    fftBuf.re[i] = (double)samples[i] * w;
    fftBuf.im[i] = 0.0;
  }
  complexFFT(fftBuf, FFT_LEN_EXPONENT, true);

  // ----- VTTF curve ---------------------------------------------------------
  // Normalize so the in-band peak shows up at 0 dB. Without this the
  // transfer-function magnitudes are on a different scale than the audio
  // FFT and the curve walks off the top/bottom of the window.
  const double vttfFreqStep = (double)AUDIO_SAMPLING_RATE_HZ / (double)VTTF_LEN;
  double vttfPeakDb = 0.0;
  if (haveVttf) {
    int binMax = (int)(fMax_Hz / vttfFreqStep);
    if (binMax >= VTTF_LEN / 2) binMax = VTTF_LEN / 2 - 1;
    double peakMag = 1e-12;
    for (int i = 1; i <= binMax; ++i) {
      double m = s_vttf.getMagnitude(i);
      if (m > peakMag) peakMag = m;
    }
    vttfPeakDb = 20.0 * std::log10(peakMag);

    std::vector<ImVec2> vttfPts;
    vttfPts.reserve((int)canvasW + 1);
    const double mag0 = 1e-9;
    for (int px = 0; px <= (int)canvasW; ++px) {
      double f = freqForPx(px);
      double bin = f / vttfFreqStep;
      int i0 = (int)bin;
      if (i0 < 0) i0 = 0;
      if (i0 >= VTTF_LEN / 2 - 1) i0 = VTTF_LEN / 2 - 2;
      double t = bin - (double)i0;
      double m = (1.0 - t) * s_vttf.getMagnitude(i0) +
                 t * s_vttf.getMagnitude(i0 + 1);
      if (m < mag0) m = mag0;
      double db = 20.0 * std::log10(m) - vttfPeakDb;  // peak -> 0 dB
      vttfPts.push_back(ImVec2(canvasMin.x + (float)px, yForDb(db)));
    }
    if (vttfPts.size() >= 2) {
      dl->AddPolyline(vttfPts.data(), (int)vttfPts.size(), colVttf,
                      ImDrawFlags_None, 1.5f);
    }
  }

  // ----- Harmonic stem plot -------------------------------------------------
  // Vertical line at each n*f0 rising from the panel floor to the VTTF
  // value at that frequency, plus a filled dot at the top — the classic
  // source-filter view of how the tract amplifies each harmonic of the
  // current fundamental. Falls back to floor-anchored stubs when no VTTF
  // is available so the harmonic positions are still visible.
  if (f0_Hz > 20.0 && f0_Hz < fMax_Hz) {
    int maxHarmonic = (int)(fMax_Hz / f0_Hz);
    if (maxHarmonic > 200) maxHarmonic = 200;  // avoid pathological loops
    for (int n = 1; n <= maxHarmonic; ++n) {
      double f = (double)n * f0_Hz;
      if (f < fMin_Hz) continue;  // log mode trims sub-50-Hz harmonics
      float x = xForFreq(f);
      bool isFundamental = (n == 1);
      ImU32 col = isFundamental ? colFundamental : colHarmonic;
      float thickness = isFundamental ? 2.5f : 1.0f;
      float dotRadius = isFundamental ? 4.0f : 2.0f;

      float yTop;
      if (haveVttf) {
        double bin = f / vttfFreqStep;
        int i0 = (int)bin;
        if (i0 < 0) i0 = 0;
        if (i0 >= VTTF_LEN / 2 - 1) i0 = VTTF_LEN / 2 - 2;
        double t = bin - (double)i0;
        double m = (1.0 - t) * s_vttf.getMagnitude(i0) +
                   t * s_vttf.getMagnitude(i0 + 1);
        if (m < 1e-9) m = 1e-9;
        double db = 20.0 * std::log10(m) - vttfPeakDb;
        yTop = yForDb(db);
      } else {
        yTop = canvasMax.y - (isFundamental ? 24.0f : 8.0f);
      }
      dl->AddLine(ImVec2(x, canvasMax.y), ImVec2(x, yTop), col, thickness);
      dl->AddCircleFilled(ImVec2(x, yTop), dotRadius, col);
    }

    // Label the fundamental. Positioned below the formant labels (which
    // start at canvasMin.y + 2 and stack vertically at 14 px each — F1..F4
    // occupy y offsets 0..3*14) so the two label clusters don't collide.
    float xf0 = xForFreq(f0_Hz);
    char flabel[40];
    std::snprintf(flabel, sizeof(flabel), "f0 = %d Hz", (int)(f0_Hz + 0.5));
    dl->AddText(ImVec2(xf0 + 4.0f, canvasMin.y + 2.0f + 4.0f * 14.0f),
                colFundamental, flabel);
  }

  // ----- Audio FFT curve ----------------------------------------------------
  std::vector<ImVec2> pts;
  pts.reserve((int)canvasW + 1);
  const double mag0 = 1e-6;
  for (int px = 0; px <= (int)canvasW; ++px) {
    double f = freqForPx(px);
    double bin = f / freqStep_Hz;
    int i0 = (int)bin;
    if (i0 < 0) i0 = 0;
    if (i0 >= FFT_LEN - 1) i0 = FFT_LEN - 2;
    double t = bin - (double)i0;
    double m = (1.0 - t) * fftBuf.getMagnitude(i0) +
               t * fftBuf.getMagnitude(i0 + 1);
    if (m < mag0) m = mag0;
    double db = 20.0 * std::log10(m);
    pts.push_back(ImVec2(canvasMin.x + (float)px, yForDb(db)));
  }
  if (pts.size() >= 2) {
    dl->AddPolyline(pts.data(), (int)pts.size(), colCurve, ImDrawFlags_None,
                    1.5f);
  }

  // ----- Formant markers ----------------------------------------------------
  // Drawn last so the labels sit on top of everything else. Suppressed
  // when getFormants reported a complete closure (the peaks aren't formants
  // in the usual sense then).
  if (!isClosure) {
    for (int i = 0; i < numFormants; ++i) {
      double f = formantFreq[i];
      if (f < fMin_Hz || f > fMax_Hz) continue;
      float x = xForFreq(f);
      dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colFormant,
                  1.0f);
      char label[32];
      std::snprintf(label, sizeof(label), "F%d %d Hz", i + 1, (int)(f + 0.5));
      dl->AddText(ImVec2(x + 3.0f, canvasMin.y + 2.0f + (float)i * 14.0f),
                  colFormant, label);
    }
  }

  dl->AddRect(canvasMin, canvasMax, colBorder);

  // Piano keyboard occupies the strip below the spectrum. Drawn last so
  // its rects sit on top of the panel background but use the full-width
  // canvas (canvasMaxFull) for its lower bound.
  if (canvasMaxFull.y > canvasMax.y + 1.0f) {
    drawPianoKeyboard(dl, ImVec2(canvasMin.x, canvasMax.y), canvasMaxFull,
                      fMin_Hz, fMax_Hz, scale);
    dl->AddRect(ImVec2(canvasMin.x, canvasMax.y), canvasMaxFull, colBorder);
  }
}

}  // namespace

// Compact two-segment selector drawn inline. Each cell is a borderless
// button; the active segment is filled with the active-button color so
// the pair reads as one switch rather than two unrelated buttons.
template <typename T>
bool segmentedButton(const char* idScope, const char* const* labels,
                     const T* values, int count, T& current, ImVec2 cellSize) {
  bool changed = false;
  ImGui::PushID(idScope);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
  for (int i = 0; i < count; ++i) {
    if (i > 0) ImGui::SameLine();
    bool selected = (values[i] == current);
    ImVec4 base = ImGui::GetStyleColorVec4(
        selected ? ImGuiCol_ButtonActive : ImGuiCol_Button);
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    ImGui::PushID(i);
    if (ImGui::Button(labels[i], cellSize)) {
      current = values[i];
      changed = true;
    }
    ImGui::PopID();
    ImGui::PopStyleColor(3);
  }
  ImGui::PopStyleVar(2);
  ImGui::PopID();
  return changed;
}

void renderSpectrumPanel(AudioHistory& history, ComplexSignal& fft,
                         VocalTract* uiTract, double f0_Hz) {
  ImGui::Begin("Primary Spectrum");

  // Frequency-axis mode. Static so the user's choice persists across
  // frames. Log by default — gives pitch-uniform spacing that pairs
  // naturally with the piano keyboard along the bottom of the panel.
  static FreqScale s_scale = FreqScale::Log;

  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  drawSpectrum(ImGui::GetWindowDrawList(), history, fft, uiTract, f0_Hz,
               s_scale, canvasMin, canvasMax);
  ImGui::Dummy(avail);

  // Overlay the scale selector in the top-right of the canvas. The
  // buttons are submitted after the spectrum draw calls so they land on
  // top in the draw list; SetCursorScreenPos pulls the layout cursor
  // back over the just-claimed Dummy area, and the buttons fit inside
  // it so the panel does not auto-grow.
  static const char* const kLabels[] = {"lin", "log"};
  static const FreqScale kValues[] = {FreqScale::Linear, FreqScale::Log};
  const ImVec2 cell(28.0f, 18.0f);
  const float pad = 6.0f;
  ImVec2 btnPos(canvasMax.x - 2.0f * cell.x - pad, canvasMin.y + pad);
  ImGui::SetCursorScreenPos(btnPos);
  segmentedButton("##spec_scale", kLabels, kValues, 2, s_scale, cell);

  ImGui::End();
}

}  // namespace live
