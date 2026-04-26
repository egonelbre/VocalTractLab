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

void drawSpectrum(ImDrawList* dl, AudioHistory& history, ComplexSignal& fftBuf,
                  VocalTract* uiTract, double f0_Hz, ImVec2 canvasMin,
                  ImVec2 canvasMax) {
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

  const double fMin_Hz = 0.0;
  const double fMax_Hz = 8000.0;
  const double dbMin = -90.0;
  const double dbMax = 0.0;
  const double freqStep_Hz = (double)AUDIO_SAMPLING_RATE_HZ / (double)FFT_LEN;

  auto xForFreq = [&](double f) {
    return canvasMin.x +
           canvasW * (float)((f - fMin_Hz) / (fMax_Hz - fMin_Hz));
  };
  auto yForDb = [&](double db) {
    if (db < dbMin) db = dbMin;
    if (db > dbMax) db = dbMax;
    return canvasMax.y -
           canvasH * (float)((db - dbMin) / (dbMax - dbMin));
  };

  // Grid + axis labels.
  for (int khz = 1; khz < (int)(fMax_Hz / 1000.0); ++khz) {
    float x = xForFreq((double)khz * 1000.0);
    dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%dk", khz);
    dl->AddText(ImVec2(x + 2.0f, canvasMax.y - 14.0f), colText, label);
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
      double f = fMin_Hz + (fMax_Hz - fMin_Hz) * (double)px / (double)canvasW;
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
    double f = fMin_Hz + (fMax_Hz - fMin_Hz) * (double)px / (double)canvasW;
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
}

}  // namespace

void renderSpectrumPanel(AudioHistory& history, ComplexSignal& fft,
                         VocalTract* uiTract, double f0_Hz) {
  ImGui::Begin("Primary Spectrum");
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  drawSpectrum(ImGui::GetWindowDrawList(), history, fft, uiTract, f0_Hz,
               canvasMin, canvasMax);
  ImGui::Dummy(avail);
  ImGui::End();
}

}  // namespace live
