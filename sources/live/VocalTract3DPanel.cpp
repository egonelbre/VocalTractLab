#include "VocalTract3DPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Geometry.h"
#include "imgui.h"

namespace live {

namespace {

struct Camera3D {
  float yaw_deg = 25.0f;
  float pitch_deg = 12.0f;
  float distance_cm = 30.0f;  // > 0; smaller = bigger model on screen
};

struct Projected3D {
  ImVec2 screen;
  float depth;
  bool visible;
};

Projected3D project3D(const Camera3D& cam, Point3D p, ImVec2 canvasCenter,
                      float focal_px) {
  // Recenter the model so it rotates around its bounding-box centre.
  double x = p.x - (-2.0);
  double y = p.y - (-4.0);
  double z = p.z;
  double yaw = (double)cam.yaw_deg * M_PI / 180.0;
  double pitch = (double)cam.pitch_deg * M_PI / 180.0;
  double cy = std::cos(yaw), sy = std::sin(yaw);
  double cp = std::cos(pitch), sp = std::sin(pitch);
  double x1 = cy * x + sy * z;
  double z1 = -sy * x + cy * z;
  double y2 = cp * y - sp * z1;
  double z2 = sp * y + cp * z1;
  double x2 = x1;
  double zc = z2 + (double)cam.distance_cm;
  Projected3D out;
  out.depth = (float)zc;
  if (zc < 0.5) {
    out.visible = false;
    out.screen = ImVec2(0, 0);
    return out;
  }
  double persp = (double)focal_px / zc;
  out.screen = ImVec2(canvasCenter.x + (float)(x2 * persp),
                      canvasCenter.y - (float)(y2 * persp));
  out.visible = true;
  return out;
}

// t = 0 (near) → full color, t = 1 (far) → blend toward 'toward' (typically
// the canvas background) so back-side lines recede into the panel.
ImU32 fadeColor(ImU32 base, ImU32 toward, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float keep = 1.0f - 0.7f * t;
  float pull = 1.0f - keep;
  int r0 = (base >> IM_COL32_R_SHIFT) & 0xFF;
  int g0 = (base >> IM_COL32_G_SHIFT) & 0xFF;
  int b0 = (base >> IM_COL32_B_SHIFT) & 0xFF;
  int r1 = (toward >> IM_COL32_R_SHIFT) & 0xFF;
  int g1 = (toward >> IM_COL32_G_SHIFT) & 0xFF;
  int b1 = (toward >> IM_COL32_B_SHIFT) & 0xFF;
  int r = (int)(r0 * keep + r1 * pull);
  int g = (int)(g0 * keep + g1 * pull);
  int b = (int)(b0 * keep + b1 * pull);
  return IM_COL32(r, g, b, 255);
}

void drawWireframe(ImDrawList* dl, VocalTract* tract, const Camera3D& cam,
                   ImVec2 canvasMin, ImVec2 canvasMax, ImU32 bgColor) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW < 4.0f || canvasH < 4.0f) return;

  ImVec2 center((canvasMin.x + canvasMax.x) * 0.5f,
                (canvasMin.y + canvasMax.y) * 0.5f);
  float focal = std::min(canvasW, canvasH) * 0.9f;

  struct SurfaceSpec {
    int index;
    ImU32 color;
  };
  const SurfaceSpec specs[] = {
      {VocalTract::UPPER_COVER_TWOSIDE, IM_COL32(220, 180, 30, 255)},
      {VocalTract::LOWER_COVER_TWOSIDE, IM_COL32(180, 150, 30, 255)},
      {VocalTract::EPIGLOTTIS_TWOSIDE, IM_COL32(180, 130, 30, 255)},
      {VocalTract::UVULA_TWOSIDE, IM_COL32(220, 130, 30, 255)},
      {VocalTract::UPPER_TEETH_TWOSIDE, IM_COL32(40, 40, 40, 255)},
      {VocalTract::LOWER_TEETH_TWOSIDE, IM_COL32(40, 40, 40, 255)},
      {VocalTract::UPPER_LIP_TWOSIDE, IM_COL32(220, 80, 60, 255)},
      {VocalTract::LOWER_LIP_TWOSIDE, IM_COL32(220, 80, 60, 255)},
      {VocalTract::TONGUE, IM_COL32(220, 80, 60, 255)},
  };

  // Find global depth range so fadeColor has a meaningful t.
  float zNear = std::numeric_limits<float>::infinity();
  float zFar = -std::numeric_limits<float>::infinity();
  for (const auto& spec : specs) {
    Surface* s = &tract->surfaces[spec.index];
    for (int rib = 0; rib < s->numRibs; ++rib) {
      for (int rp = 0; rp < s->numRibPoints; ++rp) {
        Projected3D pj = project3D(cam, s->getVertex(rib, rp), center, focal);
        if (!pj.visible) continue;
        if (pj.depth < zNear) zNear = pj.depth;
        if (pj.depth > zFar) zFar = pj.depth;
      }
    }
  }
  float zSpan = zFar - zNear;
  if (zSpan < 1e-3f) zSpan = 1.0f;

  std::vector<ImVec2> pts;
  std::vector<float> depths;
  auto drawSurfaceStrip = [&](Surface* s, ImU32 color,
                              bool ribsAlongRibPoints) {
    int outerN = ribsAlongRibPoints ? s->numRibs : s->numRibPoints;
    int innerN = ribsAlongRibPoints ? s->numRibPoints : s->numRibs;
    for (int o = 0; o < outerN; ++o) {
      pts.clear();
      depths.clear();
      for (int i = 0; i < innerN; ++i) {
        Point3D p = ribsAlongRibPoints ? s->getVertex(o, i)
                                       : s->getVertex(i, o);
        Projected3D pj = project3D(cam, p, center, focal);
        if (!pj.visible) {
          if (pts.size() >= 2) {
            for (size_t j = 1; j < pts.size(); ++j) {
              float t = ((depths[j - 1] + depths[j]) * 0.5f - zNear) / zSpan;
              dl->AddLine(pts[j - 1], pts[j], fadeColor(color, bgColor, t), 1.0f);
            }
          }
          pts.clear();
          depths.clear();
          continue;
        }
        pts.push_back(pj.screen);
        depths.push_back(pj.depth);
      }
      for (size_t j = 1; j < pts.size(); ++j) {
        float t = ((depths[j - 1] + depths[j]) * 0.5f - zNear) / zSpan;
        dl->AddLine(pts[j - 1], pts[j], fadeColor(color, bgColor, t), 1.0f);
      }
    }
  };

  for (const auto& spec : specs) {
    Surface* s = &tract->surfaces[spec.index];
    drawSurfaceStrip(s, spec.color, true);
    drawSurfaceStrip(s, spec.color, false);
  }
}

}  // namespace

void renderVocalTract3DPanel(VocalTract* tract) {
  static Camera3D cam;
  ImGui::Begin("Vocal Tract 3D");

  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 canvasMin = pos;
  ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImU32 bgColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
  dl->AddRectFilled(canvasMin, canvasMax, bgColor);
  drawWireframe(dl, tract, cam, canvasMin, canvasMax, bgColor);
  dl->AddRect(canvasMin, canvasMax, ImGui::GetColorU32(ImGuiCol_Border));

  ImGui::SetCursorScreenPos(canvasMin);
  ImGui::InvisibleButton("##vt3d_canvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight);
  if (ImGui::IsItemActive()) {
    ImVec2 d = ImGui::GetIO().MouseDelta;
    cam.yaw_deg += d.x * 0.4f;
    cam.pitch_deg += d.y * 0.4f;
    if (cam.pitch_deg > 80.0f) cam.pitch_deg = 80.0f;
    if (cam.pitch_deg < -80.0f) cam.pitch_deg = -80.0f;
  }
  if (ImGui::IsItemHovered()) {
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      cam.distance_cm *= std::exp(-wheel * 0.1f);
      if (cam.distance_cm < 8.0f) cam.distance_cm = 8.0f;
      if (cam.distance_cm > 120.0f) cam.distance_cm = 120.0f;
    }
  }

  ImGui::SetCursorScreenPos(
      ImVec2(canvasMin.x + 6.0f, canvasMax.y - 28.0f));
  ImGui::Text(
      "yaw %.0f° pitch %.0f° dist %.0fcm  (drag to orbit, wheel to zoom)",
      cam.yaw_deg, cam.pitch_deg, cam.distance_cm);
  ImGui::SameLine();
  if (ImGui::SmallButton("reset##cam3d")) {
    cam = Camera3D{};
  }

  ImGui::End();
}

}  // namespace live
