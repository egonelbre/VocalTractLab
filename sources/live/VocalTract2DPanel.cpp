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

// Voice-quality inset layout. Mirrors the tongue-side inset but
// occupies the top-right corner of the canvas. Three handles for the
// supraglottal voice-quality atoms — AES (epilarynx narrowing, 0..1),
// TT (thyroid forward tilt, 0..1), PW (signed pharyngeal width,
// -1..+1). Same mid-baseline convention as the tongue-side inset:
// dragging up = +ve param value, dragging down = -ve. AES and TT
// stay in the upper half because they're 0..1; PW spans both halves.
constexpr int VQ_PARAM_IDX[3] = {VocalTract::AES, VocalTract::TT,
                                  VocalTract::PW};
constexpr const char* VQ_LABEL[3] = {"AES", "TT", "PW"};

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

struct VoiceQualityInset {
  ImVec2 rectMin{}, rectMax{};
  ImVec2 plotMin{}, plotMax{};
  float baselineY = 0.0f;
  float halfH = 0.0f;
  ImVec2 handlePx[3]{};
  bool valid = false;
};

VoiceQualityInset computeVoiceQualityInset(const ImVec2& canvasMin,
                                           const ImVec2& canvasMax,
                                           const double* tractParams) {
  VoiceQualityInset L{};
  // Same dimensions as the tongue-side inset for visual symmetry.
  float w = std::max(160.0f, (canvasMax.x - canvasMin.x) * 0.30f);
  float h = std::max(90.0f, (canvasMax.y - canvasMin.y) * 0.22f);
  if (canvasMax.x - canvasMin.x < 200.0f ||
      canvasMax.y - canvasMin.y < 160.0f) {
    return L;
  }
  const float margin = 8.0f;
  // Stacked just above the tongue-side inset, both anchored to the
  // bottom-right corner. The tongue-side inset uses the same height
  // formula, so this stays in sync as the canvas resizes.
  float tongueSideH = std::max(90.0f,
                               (canvasMax.y - canvasMin.y) * 0.22f);
  L.rectMax = ImVec2(canvasMax.x - margin,
                     canvasMax.y - margin - tongueSideH - margin);
  L.rectMin = ImVec2(L.rectMax.x - w, L.rectMax.y - h);
  // If the canvas isn't tall enough to hold both insets without
  // overlapping each other or running off the top, hide this one.
  if (L.rectMin.y < canvasMin.y + margin) {
    return VoiceQualityInset{};
  }
  const float padX = 10.0f, padTop = 18.0f, padBot = 14.0f;
  L.plotMin = ImVec2(L.rectMin.x + padX, L.rectMin.y + padTop);
  L.plotMax = ImVec2(L.rectMax.x - padX, L.rectMax.y - padBot);
  float plotW = L.plotMax.x - L.plotMin.x;
  float plotH = L.plotMax.y - L.plotMin.y;
  L.baselineY = (L.plotMin.y + L.plotMax.y) * 0.5f;
  L.halfH = plotH * 0.5f;
  for (int i = 0; i < 3; ++i) {
    float t = (i + 0.5f) / 3.0f;
    float x = L.plotMin.x + t * plotW;
    double v = tractParams[VQ_PARAM_IDX[i]];
    if (v < -1.0) v = -1.0;
    if (v > 1.0) v = 1.0;
    L.handlePx[i] = ImVec2(x, L.baselineY - (float)v * L.halfH);
  }
  L.valid = true;
  return L;
}

void drawVoiceQualityInset(ImDrawList* dl, const VoiceQualityInset& L,
                           int hoverVQ, int draggingVQ,
                           const double* tractParams) {
  // Panel background + border.
  dl->AddRectFilled(L.rectMin, L.rectMax, IM_COL32(20, 28, 44, 220), 4.0f);
  dl->AddRect(L.rectMin, L.rectMax, ImGui::GetColorU32(ImGuiCol_Border),
              4.0f);
  dl->AddText(ImVec2(L.rectMin.x + 8.0f, L.rectMin.y + 3.0f),
              ImGui::GetColorU32(ImGuiCol_Text), "Voice quality");
  // Frame + zero baseline.
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
  // Handles (no connecting polyline — these aren't a contour).
  ImU32 dotOutline = ImGui::GetColorU32(ImGuiCol_Text);
  ImU32 dotIdle = ImGui::GetColorU32(ImGuiCol_FrameBg);
  for (int i = 0; i < 3; ++i) {
    bool active = (draggingVQ == i);
    bool hover = (hoverVQ == i);
    ImU32 fill = active ? IM_COL32(40, 130, 230, 255)
                        : hover ? IM_COL32(255, 200, 80, 255)
                                : dotIdle;
    dl->AddCircleFilled(L.handlePx[i], 5.0f, fill);
    dl->AddCircle(L.handlePx[i], 5.0f, dotOutline, 0, 1.5f);
    // Param-name label below the bottom guide for orientation.
    dl->AddText(ImVec2(L.handlePx[i].x - 8.0f, L.plotMax.y + 2.0f),
                IM_COL32(160, 170, 190, 200), VQ_LABEL[i]);
    if (hover || active) {
      char tip[64];
      std::snprintf(tip, sizeof(tip), "%s = %+0.2f", VQ_LABEL[i],
                    tractParams[VQ_PARAM_IDX[i]]);
      dl->AddText(ImVec2(L.handlePx[i].x + 8.0f, L.handlePx[i].y - 16.0f),
                  dotOutline, tip);
    }
  }
  // Axis labels.
  ImU32 labelCol = IM_COL32(160, 170, 190, 200);
  dl->AddText(ImVec2(L.plotMin.x - 2.0f, L.plotMin.y - 2.0f), labelCol, "+1");
  dl->AddText(ImVec2(L.plotMin.x - 2.0f, L.plotMax.y - 12.0f), labelCol, "-1");
}

// Draws a small filled arrow from `from` to `to`. Used by the
// voice-quality overlay to indicate radial contraction (head pointing
// toward the centerline) or expansion (head pointing outward). The
// triangular head is auto-sized based on the line length so very
// short arrows don't get a disproportionate head.
void drawSmallArrow(ImDrawList* dl, ImVec2 from, ImVec2 to, ImU32 col,
                    float thickness) {
  ImVec2 d{to.x - from.x, to.y - from.y};
  float len = std::sqrt(d.x * d.x + d.y * d.y);
  if (len < 1.0f) return;
  d.x /= len;
  d.y /= len;
  ImVec2 perp{-d.y, d.x};
  float headLen = std::min(7.0f, len * 0.55f);
  float headW = headLen * 0.55f;
  ImVec2 base{to.x - d.x * headLen, to.y - d.y * headLen};
  ImVec2 left{base.x + perp.x * headW, base.y + perp.y * headW};
  ImVec2 right{base.x - perp.x * headW, base.y - perp.y * headW};
  dl->AddLine(from, base, col, thickness);
  dl->AddTriangleFilled(to, left, right, col);
}

// Centerline-based overlay highlighting the voice-quality zones.
// AES and PW share the same signed convention (negative = contraction,
// positive = expansion), so arrow direction reads consistently
// across both regions:
//   inward arrows (head pointing at the centerline) = contraction.
//   outward arrows = expansion.
// Region colours stay distinct so it's still obvious which gesture
// is acting:
//   AES (warm amber): epilarynx, ~0.5 to 3.0 cm above the glottis.
//   PW contraction (cyan), PW expansion (mint green).
// Arrow length scales with local intensity (Hann-weighted parameter
// magnitude). Sparse sampling keeps the overlay readable.
void drawMedialCompressionOverlay(ImDrawList* dl, VocalTract* tract,
                                  const TractView& view) {
  double aesParam = tract->params[VocalTract::AES].x;
  double pwParam  = tract->params[VocalTract::PW].x;
  if (aesParam == 0.0 && pwParam == 0.0) return;

  const double END_MARGIN_CM = 0.5;
  const double AES_END_CM = 3.0;
  double nasalPos = tract->nasalPortPos_cm;
  double aesStart = END_MARGIN_CM;
  double aesEnd = AES_END_CM;
  double oroStart = AES_END_CM;
  double oroEnd = nasalPos - END_MARGIN_CM;

  // Visual scaling. Min and max arrow lengths picked so that a small
  // perturbation (intensity ≈ 0.1) still leaves a visible nub and a
  // full-strength gesture (intensity = 1.0) reaches well past the
  // surface. Sized 1.25× the original calibration after live testing.
  const float MIN_ARROW_PX = 5.0f;
  const float MAX_ARROW_PX = 22.5f;
  // Outset moves the arrow tail / head away from the centerline so
  // the arrows don't collide with the tract outline at small
  // intensities.
  const float OUTLINE_OFFSET_PX = 6.0f;

  int N = VocalTract::NUM_CENTERLINE_POINTS;
  // Sample every 6th centerline point — N=129, so ~20 samples across
  // the full tract; only a handful sit inside any given region.
  const int STEP = 6;
  for (int i = 0; i < N; i += STEP) {
    double pos = tract->centerLine[i].pos;
    Point2D P = tract->centerLine[i].point;
    Point2D nrm = tract->centerLine[i].normal;

    // Signed Hann-weighted intensities. Negative = contraction,
    // positive = expansion (matching the param convention).
    double aes = 0.0;
    if (aesParam != 0.0 && pos > aesStart && pos < aesEnd) {
      double t = (pos - aesStart) / (aesEnd - aesStart);
      aes = aesParam * (0.5 - 0.5 * std::cos(t * 2.0 * M_PI));
    }
    double pw = 0.0;
    if (pwParam != 0.0 && pos > oroStart && pos < oroEnd) {
      double t = (pos - oroStart) / (oroEnd - oroStart);
      pw = pwParam * (0.5 - 0.5 * std::cos(t * 2.0 * M_PI));
    }
    double aesAbs = aes < 0.0 ? -aes : aes;
    double pwAbs  = pw  < 0.0 ? -pw  : pw;

    double intensity = (aesAbs > pwAbs) ? aesAbs : pwAbs;
    if (intensity < 0.05) continue;

    bool isExpansion;
    ImU32 col;
    if (aesAbs >= pwAbs) {
      // AES region — only contraction is reachable (range [-1, 0]),
      // so arrows always point inward. Warm amber.
      isExpansion = false;
      col = IM_COL32(255, 170, 70, 230);
    } else if (pw < 0.0) {
      // PW narrowing — cyan, arrows inward.
      isExpansion = false;
      col = IM_COL32(70, 200, 230, 230);
    } else {
      // PW widening — mint green, arrows outward.
      isExpansion = true;
      col = IM_COL32(120, 220, 140, 230);
    }

    // Screen positions: centerline point + a step along the normal in
    // model space, transformed independently so the perpendicular is
    // accurate after the view's uniform y-flip.
    ImVec2 sp = view.toScreen(P.x, P.y);
    ImVec2 spN = view.toScreen(P.x + nrm.x, P.y + nrm.y);
    ImVec2 perp{spN.x - sp.x, spN.y - sp.y};
    float plen = std::sqrt(perp.x * perp.x + perp.y * perp.y);
    if (plen < 0.5f) continue;
    perp.x /= plen;
    perp.y /= plen;

    float arrowPx =
        MIN_ARROW_PX + (MAX_ARROW_PX - MIN_ARROW_PX) * (float)intensity;
    float thickness = 1.75f + 1.5f * (float)intensity;

    // Draw a pair of arrows, one on each side of the centerline.
    // Contraction: tail outside, head at the centerline (with a small
    // gap so the arrowheads don't collide).
    // Expansion: tail at the centerline (small gap), head outside.
    for (int side = 0; side < 2; ++side) {
      float sign = (side == 0) ? 1.0f : -1.0f;
      ImVec2 inner{sp.x + sign * perp.x * OUTLINE_OFFSET_PX,
                   sp.y + sign * perp.y * OUTLINE_OFFSET_PX};
      ImVec2 outer{sp.x + sign * perp.x * (OUTLINE_OFFSET_PX + arrowPx),
                   sp.y + sign * perp.y * (OUTLINE_OFFSET_PX + arrowPx)};
      if (isExpansion) {
        drawSmallArrow(dl, inner, outer, col, thickness);
      } else {
        drawSmallArrow(dl, outer, inner, col, thickness);
      }
    }
  }
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
  drawMedialCompressionOverlay(dl, tract, view);

  // InvisibleButton owns the drag interaction over the canvas.
  ImGui::SetCursorScreenPos(canvasMin);
  ImGui::InvisibleButton("##vt_canvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft);
  bool itemActive = ImGui::IsItemActive();
  bool itemHovered = ImGui::IsItemHovered();

  static int draggingPoint = -1;
  static int draggingTS = -1;
  static int draggingVQ = -1;
  // Hide the tongue back handle when the synth is computing TRX/TRY for us
  // — dragging it can't change anything, so showing it is misleading.
  auto pointVisible = [&](int kind) {
    return !(autoTongueRoot && kind == CP_TONGUE_BACK);
  };
  int hoverPoint = -1;
  ImVec2 mousePx = ImGui::GetIO().MousePos;
  const float pickRadius_px = 9.0f;

  // Tongue side inset (bottom-right) and voice-quality inset (top-
  // right). Computed first so we can pick their handles before — and
  // instead of — the main control points: a click landing inside an
  // inset must never start dragging the tract outline behind it.
  TongueSideInset tsInset =
      computeTongueSideInset(canvasMin, canvasMax, tractParams);
  VoiceQualityInset vqInset =
      computeVoiceQualityInset(canvasMin, canvasMax, tractParams);
  auto inRect = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
  };
  bool mouseInInset =
      (tsInset.valid && inRect(mousePx, tsInset.rectMin, tsInset.rectMax)) ||
      (vqInset.valid && inRect(mousePx, vqInset.rectMin, vqInset.rectMax));
  int hoverTS = -1;
  int hoverVQ = -1;
  if (itemHovered && draggingPoint < 0 && draggingTS < 0 && draggingVQ < 0) {
    if (tsInset.valid) {
      for (int i = 0; i < 3; ++i) {
        float dx = tsInset.handlePx[i].x - mousePx.x;
        float dy = tsInset.handlePx[i].y - mousePx.y;
        if (dx * dx + dy * dy <= pickRadius_px * pickRadius_px) {
          hoverTS = i;
          break;
        }
      }
    }
    if (hoverTS < 0 && vqInset.valid) {
      for (int i = 0; i < 3; ++i) {
        float dx = vqInset.handlePx[i].x - mousePx.x;
        float dy = vqInset.handlePx[i].y - mousePx.y;
        if (dx * dx + dy * dy <= pickRadius_px * pickRadius_px) {
          hoverVQ = i;
          break;
        }
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
    else if (hoverVQ >= 0) draggingVQ = hoverVQ;
    else if (hoverPoint >= 0) draggingPoint = hoverPoint;
  }
  if (!ImGui::IsMouseDown(0)) {
    draggingPoint = -1;
    draggingTS = -1;
    draggingVQ = -1;
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

  if (draggingVQ >= 0 && itemActive && vqInset.valid) {
    // Same mapping as the tongue-side inset. AES and TT are 0..1
    // (clamped by the per-param min/max), PW is -1..+1.
    double v = -((double)(mousePx.y - vqInset.baselineY) / vqInset.halfH);
    int idx = VQ_PARAM_IDX[draggingVQ];
    const auto& info = tract->params[idx];
    if (v < info.min) v = info.min;
    if (v > info.max) v = info.max;
    tractParams[idx] = v;
    tract->params[idx].x = v;
    tract->calculateAll();
    vqInset = computeVoiceQualityInset(canvasMin, canvasMax, tractParams);
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
  if (vqInset.valid) {
    drawVoiceQualityInset(dl, vqInset, hoverVQ, draggingVQ, tractParams);
  }
  dl->AddRect(canvasMin, canvasMax, ImGui::GetColorU32(ImGuiCol_Border));

  ImGui::End();
}

}  // namespace live
