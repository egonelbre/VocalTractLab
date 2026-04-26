#include "SpectrogramPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioEngine.h"
#include "core/Constants.h"
#include "dsp/Dsp.h"
#include "dsp/Signal.h"
#include "imgui.h"

namespace live {

namespace {

// STFT frame length. 512 samples at 48 kHz gives ~10.7 ms / ~94 Hz/bin —
// enough resolution to keep formants distinct on the 0–5 kHz axis.
constexpr int FRAME_LEN_EXPONENT = 9;
constexpr int FRAME_LEN = 1 << FRAME_LEN_EXPONENT;

// Gaussian analysis window length. ~6 ms matches the default of the
// wxWidgets gui's SpectrogramPlot — short enough to resolve singing
// vibrato, long enough to keep formants visible.
constexpr int WINDOW_LEN_PT = 288;  // ≈ 6 ms @ 48 kHz

// Frequency span of the y axis.
constexpr double VIEW_RANGE_HZ = 5000.0;

// Cell size in pixels. Coarser than 1×1 because we use ImDrawList rects
// rather than uploading a texture; 3 px keeps the rect count under ~25k
// for a typical panel size.
constexpr int CELL_W = 3;
constexpr int CELL_H = 3;

// Dynamic range of the colormap, in dB. Smaller = more contrast on quiet
// signals; larger = more dynamic range visible.
constexpr double DYNAMIC_RANGE_DB = 80.0;

// F0 (pitch) tracking range — covers singing/speech without spending YIN
// cycles on bands the synthesizer never produces.
constexpr double F0_MIN_HZ = 80.0;
constexpr double F0_MAX_HZ = 600.0;
constexpr int F0_TAU_MIN = (int)((double)AUDIO_SAMPLING_RATE_HZ / F0_MAX_HZ);
constexpr int F0_TAU_MAX =
    (int)((double)AUDIO_SAMPLING_RATE_HZ / F0_MIN_HZ) + 1;
constexpr int F0_WINDOW_LEN = 768;  // ≈ 16 ms @ 48 kHz
// CMNDF "voicing" threshold from de Cheveigné & Kawahara 2002. Values below
// the threshold are treated as confidently periodic.
constexpr double F0_THRESHOLD = 0.15;
// Re-estimate every Nth spectrogram column. Keeps total YIN cost ≈ 10 ms
// per panel render even on a wide panel.
constexpr int F0_COLUMN_HOP = 4;

// CMNDF-style YIN F0 detector over a single window. Returns 0.0 for
// unvoiced (no candidate below threshold and no clear minimum) so callers
// can break the F0-track polyline cleanly. Reuses static scratch buffers
// to avoid per-call allocation.
double estimateF0(const float* x) {
  static std::vector<double> d;
  static std::vector<double> cmndf;
  if ((int)d.size() != F0_TAU_MAX + 1) d.assign(F0_TAU_MAX + 1, 0.0);
  if ((int)cmndf.size() != F0_TAU_MAX + 1) cmndf.assign(F0_TAU_MAX + 1, 0.0);

  // Energy gate: silence in, silence out. Spares the inner loop on quiet
  // history (e.g. before the first audio chunk has been produced).
  double energy = 0.0;
  for (int j = 0; j < F0_WINDOW_LEN; ++j) energy += (double)x[j] * x[j];
  if (energy < 1e-6) return 0.0;

  // d(tau) = sum_{j=0}^{W-1} (x[j] - x[j+tau])^2
  for (int tau = F0_TAU_MIN; tau <= F0_TAU_MAX; ++tau) {
    double sum = 0.0;
    for (int j = 0; j < F0_WINDOW_LEN; ++j) {
      double diff = (double)x[j] - (double)x[j + tau];
      sum += diff * diff;
    }
    d[tau] = sum;
  }
  // Cumulative-mean normalized difference: cmndf(tau) =
  //     d(tau) * tau / sum_{i=1..tau} d(i)
  double running = 0.0;
  cmndf[0] = 1.0;
  for (int tau = 1; tau < F0_TAU_MIN; ++tau) cmndf[tau] = 1.0;  // unused
  for (int tau = F0_TAU_MIN; tau <= F0_TAU_MAX; ++tau) {
    running += d[tau];
    cmndf[tau] = (running > 1e-12) ? d[tau] * (double)tau / running : 1.0;
  }

  // First tau in [tauMin, tauMax) where cmndf dips below the threshold,
  // then walk down to the local minimum. Skipping past higher-tau
  // candidates is what makes YIN robust against octave errors.
  int bestTau = -1;
  for (int tau = F0_TAU_MIN; tau < F0_TAU_MAX; ++tau) {
    if (cmndf[tau] < F0_THRESHOLD) {
      while (tau + 1 <= F0_TAU_MAX && cmndf[tau + 1] < cmndf[tau]) ++tau;
      bestTau = tau;
      break;
    }
  }
  if (bestTau < 0) return 0.0;  // unvoiced

  // Parabolic interpolation around the discrete minimum for sub-sample
  // tau resolution; without this F0 quantizes visibly to (sampleRate/tau)
  // bins of several Hz.
  double tauRefined = (double)bestTau;
  if (bestTau > F0_TAU_MIN && bestTau < F0_TAU_MAX) {
    double s0 = cmndf[bestTau - 1];
    double s1 = cmndf[bestTau];
    double s2 = cmndf[bestTau + 1];
    double denom = s0 - 2.0 * s1 + s2;
    if (std::fabs(denom) > 1e-9) {
      tauRefined = (double)bestTau + 0.5 * (s0 - s2) / denom;
    }
  }
  if (tauRefined < 1.0) return 0.0;
  return (double)AUDIO_SAMPLING_RATE_HZ / tauRefined;
}

// Gauss window of length n with sigma chosen the same way as
// graphing/SpectrogramPlot::getGaussWindow — narrow enough that the tails
// die off well before the frame ends.
void buildGaussWindow(std::vector<double>& w, int n) {
  w.resize(n);
  const double sigma = 0.4;
  int N2 = (n - 1) / 2;
  double den = 2.0 * sigma * sigma * (double)N2 * (double)N2;
  if (den <= 0.0) {
    std::fill(w.begin(), w.end(), 1.0);
    return;
  }
  for (int i = 0; i < n; ++i) {
    double d = (double)(i - N2);
    w[i] = std::exp(-d * d / den);
  }
}

void drawSpectrogram(ImDrawList* dl, AudioHistory& history, double f0_Hz,
                     ImVec2 canvasMin, ImVec2 canvasMax) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW < (float)CELL_W * 2.0f || canvasH < (float)CELL_H * 2.0f) {
    dl->AddRectFilled(canvasMin, canvasMax,
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    return;
  }

  const ImU32 colBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Text, 0.22f);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 colBorder = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 colHarmonic = ImGui::GetColorU32(ImGuiCol_PlotHistogramHovered,
                                                0.55f);
  const ImVec4 textVec = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  dl->AddRectFilled(canvasMin, canvasMax, colBg);

  // Grab the latest AUDIO_HISTORY_SIZE samples. The window covers whatever
  // the engine has produced so far; older bins are zero (silent).
  static std::array<float, AUDIO_HISTORY_SIZE> samples;
  history.copyLatest(samples.data(), AUDIO_HISTORY_SIZE);

  static std::vector<double> window;
  if ((int)window.size() != WINDOW_LEN_PT) {
    buildGaussWindow(window, WINDOW_LEN_PT);
  }

  int numCols = std::max(1, (int)(canvasW / (float)CELL_W));
  int numRows = std::max(1, (int)(canvasH / (float)CELL_H));

  // Number of FFT bins covering [0, VIEW_RANGE_HZ]. Linear axis; cell row k
  // (0 = top of canvas) maps to a band of these bins.
  double binStep_Hz = (double)AUDIO_SAMPLING_RATE_HZ / (double)FRAME_LEN;
  int numVisBins = (int)(VIEW_RANGE_HZ / binStep_Hz);
  if (numVisBins < 1) numVisBins = 1;
  if (numVisBins >= FRAME_LEN / 2) numVisBins = FRAME_LEN / 2 - 1;

  // Reuse one FFT scratch buffer across columns. realFFT writes the
  // half-spectrum into re[0..N/2] and im[0..N/2].
  static ComplexSignal frame(FRAME_LEN);
  if (frame.N != FRAME_LEN) frame.reset(FRAME_LEN);

  // 240 dB matches the wxWidgets reference level — the absolute scale of
  // the FFT depends only on the window energy, not the speaker volume,
  // which we want: gain changes don't shift the colormap.
  const double maxValue_dB = 240.0;
  const double dbFloor = maxValue_dB - DYNAMIC_RANGE_DB;
  const double dbScale = 1.0 / DYNAMIC_RANGE_DB;

  // Cache the per-column intensity grid so the rect draw loop doesn't have
  // to re-read FFT magnitudes when interpolating across rows.
  static std::vector<float> grid;  // numCols * numRows, intensity 0..1
  grid.assign((size_t)numCols * (size_t)numRows, 0.0f);

  for (int col = 0; col < numCols; ++col) {
    // Window center walks across the full history buffer left -> right.
    int centerSample =
        (int)((int64_t)col * (AUDIO_HISTORY_SIZE - 1) / std::max(1, numCols - 1));
    int startSample = centerSample - WINDOW_LEN_PT / 2;

    for (int k = 0; k < WINDOW_LEN_PT; ++k) {
      int idx = startSample + k;
      double s = (idx >= 0 && idx < AUDIO_HISTORY_SIZE)
                     ? (double)samples[(size_t)idx]
                     : 0.0;
      frame.re[k] = s * window[(size_t)k];
      frame.im[k] = 0.0;
    }
    for (int k = WINDOW_LEN_PT; k < FRAME_LEN; ++k) {
      frame.re[k] = 0.0;
      frame.im[k] = 0.0;
    }
    // normalize=false: amplitude scaling stays consistent regardless of
    // FRAME_LEN, so the colormap reference dB doesn't move when we
    // change frame size.
    realFFT(frame, FRAME_LEN_EXPONENT, false);

    for (int row = 0; row < numRows; ++row) {
      // Top row = highest visible frequency.
      double rowFrac = 1.0 - (double)row / (double)numRows;
      int binStart = (int)(rowFrac * numVisBins);
      int binEnd = (int)((rowFrac + 1.0 / (double)numRows) * numVisBins);
      if (binEnd <= binStart) binEnd = binStart + 1;
      if (binEnd > numVisBins) binEnd = numVisBins;
      double maxMagSq = 0.0;
      for (int b = binStart; b < binEnd; ++b) {
        double r = frame.re[b];
        double im = frame.im[b];
        double m2 = r * r + im * im;
        if (m2 > maxMagSq) maxMagSq = m2;
      }
      if (maxMagSq < 1e-30) maxMagSq = 1e-30;
      double db = 10.0 * std::log10(maxMagSq);
      double t = (db - dbFloor) * dbScale;
      if (t < 0.0) t = 0.0;
      if (t > 1.0) t = 1.0;
      grid[(size_t)col * (size_t)numRows + (size_t)row] = (float)t;
    }
  }

  // Render. Use intensity as alpha against the panel background, with
  // (r,g,b) = the active text color so the result reads correctly under
  // either light or dark themes.
  for (int col = 0; col < numCols; ++col) {
    float x0 = canvasMin.x + (float)(col * CELL_W);
    float x1 = x0 + (float)CELL_W;
    if (x1 > canvasMax.x) x1 = canvasMax.x;
    for (int row = 0; row < numRows; ++row) {
      float intensity = grid[(size_t)col * (size_t)numRows + (size_t)row];
      if (intensity < 0.02f) continue;  // skip near-empty cells
      float y0 = canvasMin.y + (float)(row * CELL_H);
      float y1 = y0 + (float)CELL_H;
      if (y1 > canvasMax.y) y1 = canvasMax.y;
      ImVec4 c(textVec.x, textVec.y, textVec.z, intensity);
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                        ImGui::ColorConvertFloat4ToU32(c));
    }
  }

  // Frequency-axis ticks (kHz lines + labels), drawn over the heatmap so
  // they remain readable.
  auto yForFreq = [&](double f) {
    return canvasMax.y - canvasH * (float)(f / VIEW_RANGE_HZ);
  };
  for (int khz = 1; khz < (int)(VIEW_RANGE_HZ / 1000.0); ++khz) {
    float y = yForFreq((double)khz * 1000.0);
    dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%dk", khz);
    dl->AddText(ImVec2(canvasMin.x + 2.0f, y - 14.0f), colText, label);
  }

  // Harmonic guide lines: horizontal markers at n*f0 to make harmonic
  // structure pop out of the heatmap. Bounded so a near-DC f0 doesn't
  // spam thousands of lines.
  if (f0_Hz > 20.0 && f0_Hz < VIEW_RANGE_HZ) {
    int maxHarmonic = (int)(VIEW_RANGE_HZ / f0_Hz);
    if (maxHarmonic > 100) maxHarmonic = 100;
    for (int n = 1; n <= maxHarmonic; ++n) {
      float y = yForFreq((double)n * f0_Hz);
      dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), colHarmonic,
                  1.0f);
    }
  }

  // ----- F0 track -----------------------------------------------------------
  // YIN per column at a coarse hop, plus a numeric readout in the corner so
  // you can verify the synthesizer is hitting the requested fundamental.
  // Drawn after the harmonic guides so the track sits on top.
  const ImU32 colF0 = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);
  std::vector<float> f0Track((size_t)numCols, 0.0f);
  // Window placement: data range is [start, start + W + tauMax]. We center
  // that range on the column's centerSample so the F0 estimate aligns
  // visually with the spectrogram column.
  const int dataSpan = F0_WINDOW_LEN + F0_TAU_MAX;
  for (int col = 0; col < numCols; col += F0_COLUMN_HOP) {
    int centerSample =
        (int)((int64_t)col * (AUDIO_HISTORY_SIZE - 1) /
              std::max(1, numCols - 1));
    int windowStart = centerSample - dataSpan / 2;
    if (windowStart < 0 || windowStart + dataSpan > AUDIO_HISTORY_SIZE) {
      continue;
    }
    f0Track[(size_t)col] =
        (float)estimateF0(samples.data() + windowStart);
  }
  // Stitch sampled columns into per-column track via linear interp; skip
  // segments where either endpoint is unvoiced so the polyline doesn't
  // dip to 0 Hz.
  for (int seg = 0; seg + F0_COLUMN_HOP < numCols; seg += F0_COLUMN_HOP) {
    float a = f0Track[(size_t)seg];
    float b = f0Track[(size_t)std::min(seg + F0_COLUMN_HOP, numCols - 1)];
    if (a <= 0.0f || b <= 0.0f) continue;
    int last = std::min(seg + F0_COLUMN_HOP, numCols - 1);
    for (int c = seg + 1; c < last; ++c) {
      float t = (float)(c - seg) / (float)(last - seg);
      f0Track[(size_t)c] = a + t * (b - a);
    }
  }
  // Draw segments wherever consecutive columns are both voiced.
  for (int col = 1; col < numCols; ++col) {
    float a = f0Track[(size_t)(col - 1)];
    float b = f0Track[(size_t)col];
    if (a <= 0.0f || b <= 0.0f) continue;
    if (a > VIEW_RANGE_HZ || b > VIEW_RANGE_HZ) continue;
    float x0 = canvasMin.x + (float)((col - 1) * CELL_W);
    float x1 = canvasMin.x + (float)(col * CELL_W);
    dl->AddLine(ImVec2(x0, yForFreq(a)), ImVec2(x1, yForFreq(b)), colF0, 2.0f);
  }
  // Median of the voiced columns is robust against single-column glitches
  // (octave halving on a transient, etc.).
  static std::vector<float> voiced;
  voiced.clear();
  for (float f : f0Track) {
    if (f > 0.0f) voiced.push_back(f);
  }
  if (!voiced.empty()) {
    std::nth_element(voiced.begin(), voiced.begin() + voiced.size() / 2,
                     voiced.end());
    float medianF0 = voiced[voiced.size() / 2];
    char flabel[40];
    std::snprintf(flabel, sizeof(flabel), "F0 detected: %d Hz   target: %d Hz",
                  (int)(medianF0 + 0.5f), (int)(f0_Hz + 0.5));
    dl->AddText(ImVec2(canvasMin.x + 4.0f, canvasMax.y - 14.0f), colF0,
                flabel);
  } else {
    dl->AddText(ImVec2(canvasMin.x + 4.0f, canvasMax.y - 14.0f), colText,
                "F0 detected: --");
  }

  // Time axis: history coverage at the bottom-right corner.
  double durationMs =
      1000.0 * (double)AUDIO_HISTORY_SIZE / (double)AUDIO_SAMPLING_RATE_HZ;
  char tlabel[32];
  std::snprintf(tlabel, sizeof(tlabel), "%.0f ms", durationMs);
  dl->AddText(ImVec2(canvasMax.x - 60.0f, canvasMax.y - 14.0f), colText,
              tlabel);

  dl->AddRect(canvasMin, canvasMax, colBorder);
}

}  // namespace

void renderSpectrogramPanel(AudioHistory& history, double f0_Hz) {
  ImGui::Begin("Spectrogram");
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  drawSpectrogram(ImGui::GetWindowDrawList(), history, f0_Hz, canvasMin,
                  canvasMax);
  ImGui::Dummy(avail);
  ImGui::End();
}

}  // namespace live
