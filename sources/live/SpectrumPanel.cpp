#include "SpectrumPanel.h"

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

void drawSpectrum(ImDrawList* dl, AudioHistory& history, ComplexSignal& fftBuf,
                  ImVec2 canvasMin, ImVec2 canvasMax) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW <= 1.0f || canvasH <= 1.0f) return;

  const ImU32 colBg = IM_COL32(245, 245, 245, 255);
  const ImU32 colGrid = IM_COL32(200, 200, 200, 255);
  const ImU32 colCurve = IM_COL32(40, 80, 200, 255);
  const ImU32 colText = IM_COL32(80, 80, 80, 255);
  dl->AddRectFilled(canvasMin, canvasMax, colBg);

  static std::array<float, FFT_LEN> samples;
  history.copyLatest(samples.data(), FFT_LEN);

  fftBuf.reset(FFT_LEN);
  for (int i = 0; i < FFT_LEN; ++i) {
    double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (double)(FFT_LEN - 1));
    fftBuf.re[i] = (double)samples[i] * w;
    fftBuf.im[i] = 0.0;
  }
  complexFFT(fftBuf, FFT_LEN_EXPONENT, true);

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
  dl->AddRect(canvasMin, canvasMax, IM_COL32(150, 150, 150, 255));
}

}  // namespace

void renderSpectrumPanel(AudioHistory& history, ComplexSignal& fft) {
  ImGui::Begin("Primary Spectrum");
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  drawSpectrum(ImGui::GetWindowDrawList(), history, fft, canvasMin, canvasMax);
  ImGui::Dummy(avail);
  ImGui::End();
}

}  // namespace live
