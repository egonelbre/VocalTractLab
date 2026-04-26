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

  // Time axis: history coverage at the bottom-left corner.
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
