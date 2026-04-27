#include "VocalTract2DPanel.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ControlPoints.h"
#include "TractView.h"
#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Geometry.h"
#include "imgui.h"

namespace live {

namespace {

// Tongue-side inset layout. The inset is a small panel docked to the
// bottom-right of the main vocal-tract canvas. It plots the three
// tongue side elevation parameters (TS1 back / TS2 mid / TS3 front)
// against a horizontal "midline" baseline: dragging a handle up raises
// that side of the tongue, dragging it below the baseline (only TS3
// can go negative) lowers it for lateral sounds. Direct-on-param
// values are used for the Y axis — same convention as the slider in
// the Articulation panel — so positive maps to "raised".
constexpr int TS_PARAM_IDX[3] = {VocalTract::TS1, VocalTract::TS2,
                                  VocalTract::TS3};
constexpr const char* TS_LABEL[3] = {"TS1 (back)", "TS2 (mid)", "TS3 (front)"};

struct TongueSideInset {
  ImVec2 rectMin{}, rectMax{};   // outer panel bounds
  ImVec2 plotMin{}, plotMax{};   // inner plot area (inside padding)
  float baselineY = 0.0f;
  float halfH = 0.0f;            // pixels per 1.0 of param value
  ImVec2 handlePx[3]{};
  bool valid = false;
};

TongueSideInset computeTongueSideInset(const ImVec2& canvasMin,
                                       const ImVec2& canvasMax,
                                       const double* tractParams) {
  TongueSideInset L{};
  // Sized as a fraction of the canvas with a sensible minimum so the
  // handles stay pickable even when the panel is dragged narrow.
  float w = std::max(160.0f, (canvasMax.x - canvasMin.x) * 0.30f);
  float h = std::max(90.0f, (canvasMax.y - canvasMin.y) * 0.22f);
  if (canvasMax.x - canvasMin.x < 200.0f ||
      canvasMax.y - canvasMin.y < 160.0f) {
    return L;  // panel too small, skip the inset
  }
  const float margin = 8.0f;
  L.rectMax = ImVec2(canvasMax.x - margin, canvasMax.y - margin);
  L.rectMin = ImVec2(L.rectMax.x - w, L.rectMax.y - h);
  const float padX = 10.0f, padTop = 18.0f, padBot = 14.0f;
  L.plotMin = ImVec2(L.rectMin.x + padX, L.rectMin.y + padTop);
  L.plotMax = ImVec2(L.rectMax.x - padX, L.rectMax.y - padBot);
  float plotW = L.plotMax.x - L.plotMin.x;
  float plotH = L.plotMax.y - L.plotMin.y;
  L.baselineY = (L.plotMin.y + L.plotMax.y) * 0.5f;
  L.halfH = plotH * 0.5f;
  // Three handles evenly distributed across the plot width, back→front.
  for (int i = 0; i < 3; ++i) {
    float t = (i + 0.5f) / 3.0f;  // 1/6, 1/2, 5/6
    float x = L.plotMin.x + t * plotW;
    double v = tractParams[TS_PARAM_IDX[i]];
    if (v < -1.0) v = -1.0;
    if (v > 1.0) v = 1.0;
    L.handlePx[i] = ImVec2(x, L.baselineY - (float)v * L.halfH);
  }
  L.valid = true;
  return L;
}

void drawTongueSideInset(ImDrawList* dl, const TongueSideInset& L,
                         int hoverTS, int draggingTS,
                         const double* tractParams) {
  // Panel background + border.
  dl->AddRectFilled(L.rectMin, L.rectMax, IM_COL32(20, 28, 44, 220), 4.0f);
  dl->AddRect(L.rectMin, L.rectMax, ImGui::GetColorU32(ImGuiCol_Border),
              4.0f);
  // Title strip.
  dl->AddText(ImVec2(L.rectMin.x + 8.0f, L.rectMin.y + 3.0f),
              ImGui::GetColorU32(ImGuiCol_Text), "Tongue side");
  // Baseline (zero elevation) and the +/- 1.0 guides as faint dashes.
  ImU32 axisCol = IM_COL32(140, 150, 170, 180);
  ImU32 guideCol = IM_COL32(80, 90, 110, 140);
  dl->AddLine(ImVec2(L.plotMin.x, L.plotMin.y),
              ImVec2(L.plotMin.x, L.plotMax.y), guideCol, 1.0f);
  dl->AddLine(ImVec2(L.plotMin.x, L.plotMin.y),
              ImVec2(L.plotMax.x, L.plotMin.y), guideCol, 1.0f);
  dl->AddLine(ImVec2(L.plotMin.x, L.plotMax.y),
              ImVec2(L.plotMax.x, L.plotMax.y), guideCol, 1.0f);
  dl->AddLine(ImVec2(L.plotMin.x, L.baselineY),
              ImVec2(L.plotMax.x, L.baselineY), axisCol, 1.0f);
  // Polyline through handle positions so the user can read the side
  // height as a contour from back to front.
  ImU32 lineCol = IM_COL32(255, 200, 80, 220);
  dl->AddLine(L.handlePx[0], L.handlePx[1], lineCol, 1.5f);
  dl->AddLine(L.handlePx[1], L.handlePx[2], lineCol, 1.5f);
  // Handles.
  ImU32 dotOutline = ImGui::GetColorU32(ImGuiCol_Text);
  ImU32 dotIdle = ImGui::GetColorU32(ImGuiCol_FrameBg);
  for (int i = 0; i < 3; ++i) {
    bool active = (draggingTS == i);
    bool hover = (hoverTS == i);
    ImU32 fill = active ? IM_COL32(40, 130, 230, 255)
                        : hover ? IM_COL32(255, 200, 80, 255)
                                : dotIdle;
    dl->AddCircleFilled(L.handlePx[i], 5.0f, fill);
    dl->AddCircle(L.handlePx[i], 5.0f, dotOutline, 0, 1.5f);
    if (hover || active) {
      char tip[64];
      std::snprintf(tip, sizeof(tip), "%s = %+0.2f", TS_LABEL[i],
                    tractParams[TS_PARAM_IDX[i]]);
      dl->AddText(ImVec2(L.handlePx[i].x + 8.0f, L.handlePx[i].y - 16.0f),
                  dotOutline, tip);
    }
  }
  // Axis labels at the corners of the plot.
  ImU32 labelCol = IM_COL32(160, 170, 190, 200);
  dl->AddText(ImVec2(L.plotMin.x - 2.0f, L.plotMin.y - 2.0f), labelCol, "+1");
  dl->AddText(ImVec2(L.plotMin.x - 2.0f, L.plotMax.y - 12.0f), labelCol, "-1");
  dl->AddText(ImVec2(L.plotMin.x, L.plotMax.y + 2.0f), labelCol, "back");
  dl->AddText(ImVec2(L.plotMax.x - 28.0f, L.plotMax.y + 2.0f), labelCol,
              "front");
}

void appendVertex(std::vector<ImVec2>& out, const TractView& view, double x,
                  double y) {
  out.push_back(view.toScreen(x, y));
}

void drawStrip(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 color,
               float thickness) {
  if (pts.size() >= 2) {
    dl->AddPolyline(pts.data(), (int)pts.size(), color, ImDrawFlags_None,
                    thickness);
  }
}

// Pulls the same line strips that VocalTractPicture::render2D draws on the
// wxGLCanvas — upper and lower covers (with uvula / epiglottis insertions),
// the tongue mediosagittal contour, the dashed tongue circle, the teeth,
// and the lips.
void drawOutline(ImDrawList* dl, VocalTract* tract, const TractView& view) {
  // Hard-structure outlines follow the active text color so they invert
  // automatically between light and dark themes. Soft tissue (tongue, lips)
  // stays a warm coral red, which reads on either background.
  const ImU32 colMain = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 colTongueSide = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
  const ImU32 colDashed = ImGui::GetColorU32(ImGuiCol_Text, 0.40f);
  const ImU32 colTongue = IM_COL32(220, 80, 60, 255);
  const ImU32 colLip = IM_COL32(220, 80, 60, 255);
  const float thickThin = 1.0f;
  const float thickMain = 1.6f;

  std::vector<ImVec2> pts;

  // Upper cover with uvula inserted at its centerline.
  pts.clear();
  Surface* s = &tract->surfaces[VocalTract::UPPER_COVER];
  int ribPoint = s->numRibPoints - 1;
  int boundary =
      VocalTract::NUM_LARYNX_RIBS + VocalTract::NUM_PHARYNX_RIBS;
  for (int i = 0; i <= boundary; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::UVULA];
  ribPoint = s->numRibPoints - 1;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  ribPoint = 0;
  for (int i = s->numRibs - 1; i >= 0; --i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::UPPER_COVER];
  ribPoint = s->numRibPoints - 1;
  for (int i = boundary + 1; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickMain);

  // Lower cover with epiglottis inserted.
  pts.clear();
  s = &tract->surfaces[VocalTract::LOWER_COVER];
  ribPoint = s->numRibPoints - 1;
  for (int i = 0; i < VocalTract::NUM_LARYNX_RIBS - 1; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::EPIGLOTTIS];
  ribPoint = s->numRibPoints - 1;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  ribPoint = 0;
  for (int i = s->numRibs - 1; i >= 0; --i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::LOWER_COVER];
  ribPoint = s->numRibPoints - 1;
  for (int i = VocalTract::NUM_LARYNX_RIBS - 1; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickMain);

  // Tongue mediosagittal contour.
  pts.clear();
  s = &tract->surfaces[VocalTract::TONGUE];
  ribPoint = s->numRibPoints / 2;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colTongue, thickMain);

  // Tongue 1 cm side line (intersects z = -1 cm with each tongue rib).
  pts.clear();
  const double EPS = 1e-6;
  int midRibPoint = s->numRibPoints / 2;
  for (int i = 0; i < s->numRibs; ++i) {
    bool ok = false;
    Point3D found;
    for (int k = 0; k < midRibPoint; ++k) {
      Point3D Q = s->getVertex(i, k);
      Point3D R = s->getVertex(i, k + 1);
      if (Q.z <= -1.0 && R.z >= -1.0) {
        double d = R.z - Q.z;
        if (d < EPS) d = EPS;
        double t = (-1.0 - Q.z) / d;
        found = Q + (R - Q) * t;
        ok = true;
      }
    }
    if (!ok) found = s->getVertex(i, 0);
    appendVertex(pts, view, found.x, found.y);
  }
  drawStrip(dl, pts, colTongueSide, thickThin);

  // Dashed tongue body ellipse.
  {
    double mx = tract->params[VocalTract::TCX].limitedX;
    double my = tract->params[VocalTract::TCY].limitedX;
    double rx = tract->anatomy.tongueCenterRadiusX_cm;
    double ry = tract->anatomy.tongueCenterRadiusY_cm;
    constexpr int N = 64;
    for (int i = 0; i < N; ++i) {
      if ((i & 1) == 0) continue;  // gap every other segment for the dash
      double a0 = 2.0 * M_PI * (double)i / (double)N;
      double a1 = 2.0 * M_PI * (double)(i + 1) / (double)N;
      ImVec2 p0 = view.toScreen(mx + rx * std::cos(a0),
                                my + ry * std::sin(a0));
      ImVec2 p1 = view.toScreen(mx + rx * std::cos(a1),
                                my + ry * std::sin(a1));
      dl->AddLine(p0, p1, colDashed, thickThin);
    }
  }

  // Upper teeth: inner edge + cropped outer edge + first/last full rib.
  s = &tract->surfaces[VocalTract::UPPER_TEETH];
  pts.clear();
  ribPoint = 0;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  pts.clear();
  ribPoint = 2;
  for (int i = 0; i < s->numRibs - 4; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  pts.clear();
  for (int i = 0; i < 2; ++i) {
    Point3D q = s->getVertex(0, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 2, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  // Lower teeth.
  s = &tract->surfaces[VocalTract::LOWER_TEETH];
  pts.clear();
  ribPoint = 0;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  pts.clear();
  ribPoint = 2;
  for (int i = 0; i < s->numRibs - 7; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  pts.clear();
  for (int i = 0; i < 2; ++i) {
    Point3D q = s->getVertex(0, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 2, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colMain, thickThin);

  // Lips.
  s = &tract->surfaces[VocalTract::UPPER_LIP];
  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 1, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colLip, thickMain);

  s = &tract->surfaces[VocalTract::LOWER_LIP];
  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 1, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colLip, thickMain);
}

}  // namespace

void renderVocalTract2DPanel(VocalTract* tract, double* tractParams,
                             bool autoTongueRoot) {
  ImGui::Begin("Vocal Tract");

  // Mirror the UI's auto-toggle onto the tract before calculateAll so the
  // displayed geometry (and the handle positions further down) reflect
  // whatever the synth will actually use.
  tract->anatomy.automaticTongueRootCalc = autoTongueRoot;
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    tract->params[i].x = tractParams[i];
  }
  tract->calculateAll();

  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(canvasMin, canvasMax,
                    ImGui::GetColorU32(ImGuiCol_FrameBg));
  // Use the articulation-extent bounds (cached, sampled across slider
  // ranges) instead of the live bounds so the zoom stays stable while
  // the user moves sliders — otherwise the view shrinks when e.g. the
  // hyoid drops and the tract grows past the initial extent.
  const TractBounds& bounds = articulationExtentBounds(tract);
  TractView view = computeTractView(canvasMin, canvasMax, bounds);
  drawOutline(dl, tract, view);

  // InvisibleButton owns the drag interaction over the canvas.
  ImGui::SetCursorScreenPos(canvasMin);
  ImGui::InvisibleButton("##vt_canvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft);
  bool itemActive = ImGui::IsItemActive();
  bool itemHovered = ImGui::IsItemHovered();

  static int draggingPoint = -1;
  static int draggingTS = -1;
  // Hide the tongue back handle when the synth is computing TRX/TRY for us
  // — dragging it can't change anything, so showing it is misleading.
  auto pointVisible = [&](int kind) {
    return !(autoTongueRoot && kind == CP_TONGUE_BACK);
  };
  int hoverPoint = -1;
  ImVec2 mousePx = ImGui::GetIO().MousePos;
  const float pickRadius_px = 9.0f;

  // Tongue side inset (bottom-right). Computed first so we can pick its
  // handles before — and instead of — the main control points: a click
  // landing inside the inset should never start dragging the tract
  // outline that happens to live behind it.
  TongueSideInset tsInset =
      computeTongueSideInset(canvasMin, canvasMax, tractParams);
  bool mouseInInset =
      tsInset.valid &&
      mousePx.x >= tsInset.rectMin.x && mousePx.x <= tsInset.rectMax.x &&
      mousePx.y >= tsInset.rectMin.y && mousePx.y <= tsInset.rectMax.y;
  int hoverTS = -1;
  if (tsInset.valid && itemHovered && draggingPoint < 0 && draggingTS < 0) {
    for (int i = 0; i < 3; ++i) {
      float dx = tsInset.handlePx[i].x - mousePx.x;
      float dy = tsInset.handlePx[i].y - mousePx.y;
      if (dx * dx + dy * dy <= pickRadius_px * pickRadius_px) {
        hoverTS = i;
        break;
      }
    }
  }

  ControlPointInfo pts[CP_COUNT];
  bool dragging = (draggingPoint >= 0);
  for (int i = 0; i < CP_COUNT; ++i) {
    pts[i] = getControlPoint(tract, i);
    if (!pointVisible(i)) continue;
    // Don't pick a tract handle when the cursor is over the inset — the
    // inset always wins clicks in its own rect.
    if (mouseInInset) continue;
    ImVec2 sp = view.toScreen(pts[i].modelPos.x, pts[i].modelPos.y);
    float dx = sp.x - mousePx.x, dy = sp.y - mousePx.y;
    if (itemHovered && !dragging &&
        dx * dx + dy * dy <= pickRadius_px * pickRadius_px && hoverPoint < 0) {
      hoverPoint = i;
    }
  }

  if (itemHovered && ImGui::IsMouseClicked(0)) {
    if (hoverTS >= 0) draggingTS = hoverTS;
    else if (hoverPoint >= 0) draggingPoint = hoverPoint;
  }
  if (!ImGui::IsMouseDown(0)) {
    draggingPoint = -1;
    draggingTS = -1;
  }

  if (draggingTS >= 0 && itemActive && tsInset.valid) {
    // Map mouse Y back to a [-1, +1] param value, then clamp to the
    // per-parameter min/max from the speaker's anatomy file (TS1/TS2
    // are positive-only, TS3 spans both).
    double v = -((double)(mousePx.y - tsInset.baselineY) / tsInset.halfH);
    int idx = TS_PARAM_IDX[draggingTS];
    const auto& info = tract->params[idx];
    if (v < info.min) v = info.min;
    if (v > info.max) v = info.max;
    tractParams[idx] = v;
    tract->params[idx].x = v;
    tract->calculateAll();
    // Recompute the inset geometry so the dragged handle follows the
    // mouse the same frame.
    tsInset = computeTongueSideInset(canvasMin, canvasMax, tractParams);
  }

  if (draggingPoint >= 0 && itemActive) {
    ImVec2 modelTarget = view.toModel(mousePx);
    applyControlPointDrag(tract, tractParams, draggingPoint, modelTarget);
    // Recompute geometry so this frame's overlay reflects the drag.
    for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
      tract->params[i].x = tractParams[i];
    }
    tract->calculateAll();
    for (int i = 0; i < CP_COUNT; ++i) pts[i] = getControlPoint(tract, i);
  }

  const ImU32 dotOutline = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 dotIdle = ImGui::GetColorU32(ImGuiCol_FrameBg);
  for (int i = 0; i < CP_COUNT; ++i) {
    if (!pointVisible(i)) continue;
    ImVec2 sp = view.toScreen(pts[i].modelPos.x, pts[i].modelPos.y);
    bool active = (draggingPoint == i);
    bool hover = (hoverPoint == i);
    ImU32 fill = active ? IM_COL32(40, 130, 230, 255)
                        : hover ? IM_COL32(255, 200, 80, 255)
                                : dotIdle;
    dl->AddCircleFilled(sp, 5.0f, fill);
    dl->AddCircle(sp, 5.0f, dotOutline, 0, 1.5f);
    if (hover || active) {
      dl->AddText(ImVec2(sp.x + 8.0f, sp.y - 8.0f), dotOutline,
                  controlPointLabel(i));
    }
  }
  if (tsInset.valid) {
    drawTongueSideInset(dl, tsInset, hoverTS, draggingTS, tractParams);
  }
  dl->AddRect(canvasMin, canvasMax, ImGui::GetColorU32(ImGuiCol_Border));

  ImGui::End();
}

}  // namespace live
