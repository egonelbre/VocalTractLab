#include "LfPulsePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/Signal.h"
#include "glottis/LfPulse.h"
#include "imgui.h"

namespace live {

namespace {

// One period of the LF pulse is sampled at this resolution for display.
// Plenty for a smooth curve at any reasonable panel width.
constexpr int PULSE_SAMPLES = 1024;

// Compute the curve points and the value range simultaneously so the two
// curves (flow and derivative) can be drawn on a shared, padded y-axis.
void evaluatePulse(LfPulse& lf, std::vector<double>& flow,
                   std::vector<double>& deriv, double& lo, double& hi) {
  Signal s;
  lf.getPulse(s, PULSE_SAMPLES, false);
  flow.assign(s.x, s.x + PULSE_SAMPLES);
  lf.getPulse(s, PULSE_SAMPLES, true);
  deriv.assign(s.x, s.x + PULSE_SAMPLES);

  // The derivative scale is much larger than the flow scale; normalize
  // each independently so both curves use the full y-axis. Without this
  // the flow looks like a flat line under the derivative spike.
  auto normalize = [](std::vector<double>& v) {
    double a = v[0], b = v[0];
    for (double x : v) {
      if (x < a) a = x;
      if (x > b) b = x;
    }
    double range = b - a;
    if (range < 1e-12) range = 1e-12;
    for (double& x : v) x = (x - a) / range;  // [0, 1]
  };
  normalize(flow);
  normalize(deriv);
  lo = 0.0;
  hi = 1.0;
}

}  // namespace

void renderLfPulsePanel(double f0_Hz) {
  ImGui::Begin("Glottal Pulse");

  // Static so the sliders persist between frames. Initialized once via
  // the LfPulse constructor's resetParams() (default LF values).
  static LfPulse s_lf;

  ImGui::TextDisabled(
      "Reference LF source — does not drive synthesis (geometric glottis"
      " is active)");
  float oq = (float)s_lf.OQ;
  float sq = (float)s_lf.SQ;
  float tl = (float)s_lf.TL;
  ImGui::SliderFloat("OQ (open quotient)", &oq, 0.05f, 0.95f, "%.2f");
  ImGui::SliderFloat("SQ (speed quotient)", &sq, 1.0f, 5.0f, "%.2f");
  ImGui::SliderFloat("TL (spectral tilt)", &tl, 0.0f, 0.20f, "%.3f");
  if (ImGui::Button("Reset LF defaults")) {
    s_lf.resetParams();
  } else {
    s_lf.OQ = (double)oq;
    s_lf.SQ = (double)sq;
    s_lf.TL = (double)tl;
  }
  s_lf.F0 = f0_Hz > 0.0 ? f0_Hz : 120.0;

  static std::vector<double> flow, deriv;
  double lo = 0.0, hi = 1.0;
  evaluatePulse(s_lf, flow, deriv, lo, hi);

  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 4.0f || avail.y < 4.0f) {
    ImGui::End();
    return;
  }
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  ImDrawList* dl = ImGui::GetWindowDrawList();

  const ImU32 colBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Text, 0.20f);
  const ImU32 colFlow = ImGui::GetColorU32(ImGuiCol_PlotLines);
  const ImU32 colDeriv = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 colBorder = ImGui::GetColorU32(ImGuiCol_Border);
  dl->AddRectFilled(canvasMin, canvasMax, colBg);

  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;

  // Horizontal midline + zero line for the flow.
  float midY = canvasMin.y + canvasH * 0.5f;
  dl->AddLine(ImVec2(canvasMin.x, midY), ImVec2(canvasMax.x, midY), colGrid,
              1.0f);

  // Vertical guides at te and tp (open phase / max-flow point) so the
  // user can see where in the period the closure happens.
  auto xForT = [&](double t) { return canvasMin.x + canvasW * (float)t; };
  double te = s_lf.OQ;
  double tp = (te * s_lf.SQ) / (1.0 + s_lf.SQ);
  for (double tg : {tp, te}) {
    float x = xForT(tg);
    dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colGrid, 1.0f);
  }

  auto buildPolyline = [&](const std::vector<double>& v) {
    std::vector<ImVec2> pts;
    pts.reserve(PULSE_SAMPLES);
    for (int i = 0; i < PULSE_SAMPLES; ++i) {
      float x = canvasMin.x +
                canvasW * (float)i / (float)(PULSE_SAMPLES - 1);
      // Y axis: 0 at bottom, 1 at top. Inset by 4 px so the curve isn't
      // flush against the panel border.
      float yf = (float)((v[i] - lo) / (hi - lo));
      float y = canvasMax.y - 4.0f - (canvasH - 8.0f) * yf;
      pts.push_back(ImVec2(x, y));
    }
    return pts;
  };
  std::vector<ImVec2> flowPts = buildPolyline(flow);
  std::vector<ImVec2> derivPts = buildPolyline(deriv);
  if (derivPts.size() >= 2) {
    dl->AddPolyline(derivPts.data(), (int)derivPts.size(), colDeriv,
                    ImDrawFlags_None, 1.5f);
  }
  if (flowPts.size() >= 2) {
    dl->AddPolyline(flowPts.data(), (int)flowPts.size(), colFlow,
                    ImDrawFlags_None, 1.5f);
  }

  // Labels: legend in the top-left, period length in the bottom-right.
  dl->AddText(ImVec2(canvasMin.x + 4.0f, canvasMin.y + 2.0f), colFlow,
              "flow");
  dl->AddText(ImVec2(canvasMin.x + 4.0f, canvasMin.y + 16.0f), colDeriv,
              "d/dt flow");
  char tlabel[32];
  double period_ms = s_lf.F0 > 0.0 ? 1000.0 / s_lf.F0 : 0.0;
  std::snprintf(tlabel, sizeof(tlabel), "T0 = %.2f ms", period_ms);
  dl->AddText(ImVec2(canvasMax.x - 90.0f, canvasMax.y - 14.0f), colText,
              tlabel);

  dl->AddRect(canvasMin, canvasMax, colBorder);
  ImGui::Dummy(avail);
  ImGui::End();
}

}  // namespace live
