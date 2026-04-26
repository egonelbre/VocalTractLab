#include "VocalTract3DPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "TractView.h"
#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Geometry.h"
#include "imgui.h"

namespace live {

namespace {

struct Camera3D {
  float yaw_deg = 25.0f;
  float pitch_deg = 12.0f;
  float distance_cm = 0.0f;  // 0 = auto-fit on first frame
};

struct Projected3D {
  ImVec2 screen;     // perspective-projected pixel
  float camX;        // camera-space x (after rotation, before perspective)
  float camY;
  float camZ;        // depth — increases away from the camera
  bool visible;
};

Projected3D project3D(const Camera3D& cam, Point3D p, Point3D modelCenter,
                      ImVec2 canvasCenter, float focal_px) {
  // Recenter the model so it rotates around its bounding-box centre,
  // computed each frame from the surface vertices. yaw spins around y,
  // pitch tips around the post-yaw x.
  double x = p.x - modelCenter.x;
  double y = p.y - modelCenter.y;
  double z = p.z - modelCenter.z;
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
  out.camX = (float)x2;
  out.camY = (float)y2;
  out.camZ = (float)zc;
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

// Per-frame state for the panel: which render mode the user picked
// in the toolbar. Static so the choice persists across frames.
enum class RenderMode { Solid, Wireframe };

// t = 0 (near) → full color, t = 1 (far) → blend toward `toward`
// (typically the canvas background) so back-side wires recede into
// the panel. Used by the wireframe path only.
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

// Multiply RGB by a brightness factor and clamp; alpha is preserved
// untouched so translucent surfaces stay translucent regardless of
// shading.
ImU32 shade(ImU32 base, float brightness) {
  if (brightness < 0.0f) brightness = 0.0f;
  if (brightness > 1.0f) brightness = 1.0f;
  int r = (base >> IM_COL32_R_SHIFT) & 0xFF;
  int g = (base >> IM_COL32_G_SHIFT) & 0xFF;
  int b = (base >> IM_COL32_B_SHIFT) & 0xFF;
  int a = (base >> IM_COL32_A_SHIFT) & 0xFF;
  r = (int)((float)r * brightness);
  g = (int)((float)g * brightness);
  b = (int)((float)b * brightness);
  return IM_COL32(r, g, b, a);
}

void drawSolidMesh(ImDrawList* dl, VocalTract* tract, const Camera3D& cam,
                   Point3D modelCenter, ImVec2 canvasMin, ImVec2 canvasMax,
                   ImU32 /*bgColor*/) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW < 4.0f || canvasH < 4.0f) return;

  ImVec2 center((canvasMin.x + canvasMax.x) * 0.5f,
                (canvasMin.y + canvasMax.y) * 0.5f);
  float focal = std::min(canvasW, canvasH) * 0.9f;

  struct SurfaceSpec {
    int index;
    ImU32 color;  // RGB tint, alpha governs translucency
  };
  // Outer covers + uvula + epiglottis: lavender, low alpha (~45%) so
  // the inside of the mouth shows through. Teeth: light translucent
  // white. Lips / tongue: warm coral, higher alpha so they read as
  // soft tissue.
  //
  // _TWOSIDE variants of the surfaces are used for full bilateral
  // geometry: the primary surfaces only cover one half of the
  // mediosagittal plane (z >= 0), while _TWOSIDE adds the mirrored
  // half via z negation, sharing the midline vertex. They tessellate
  // as a continuous strip across the body. TONGUE has no _TWOSIDE
  // because its primary already spans both sides.
  //
  // Winding orientations of the _TWOSIDE surfaces are inconsistent
  // across the upstream code (some are swapTriangleOrientation()'d,
  // some not). Rather than tracking that per-surface we emit each
  // triangle with both windings and let the depth-bias sort give
  // back-face-then-front-face render order — the camera-facing side
  // always overdraws regardless of the source winding choice.
  const SurfaceSpec specs[] = {
      {VocalTract::UPPER_COVER_TWOSIDE, IM_COL32(180, 178, 220, 115)},
      {VocalTract::LOWER_COVER_TWOSIDE, IM_COL32(170, 168, 215, 115)},
      {VocalTract::EPIGLOTTIS_TWOSIDE,  IM_COL32(190, 150, 170, 150)},
      {VocalTract::UVULA_TWOSIDE,       IM_COL32(200, 165, 190, 150)},
      {VocalTract::UPPER_TEETH_TWOSIDE, IM_COL32(245, 245, 245, 175)},
      {VocalTract::LOWER_TEETH_TWOSIDE, IM_COL32(245, 245, 245, 175)},
      {VocalTract::UPPER_LIP_TWOSIDE,   IM_COL32(230, 130, 120, 210)},
      {VocalTract::LOWER_LIP_TWOSIDE,   IM_COL32(225, 125, 115, 210)},
      {VocalTract::TONGUE,              IM_COL32(220, 105,  95, 220)},
  };

  struct Tri {
    ImVec2 a, b, c;
    float depth;
    ImU32 color;
  };
  std::vector<Tri> tris;
  // Rough capacity: 9 _TWOSIDE-ish surfaces × ~30 ribs × ~10
  // ribpoints × 2 tris per quad × 2 windings ≈ 11000 triangles.
  // Reserving avoids reallocs as the tract is articulated.
  tris.reserve(16384);

  // Returns the camera-space face normal's z component (unnormalized
  // is fine for sign / brightness). The camera looks along -z, so a
  // negative nz means the face points toward the camera (front).
  auto faceNormalZ = [](float ax, float ay, float az,
                        float bx, float by, float bz,
                        float cx, float cy, float cz,
                        float& outLen) -> float {
    float ux = bx - ax, uy = by - ay, uz = bz - az;
    float vx = cx - ax, vy = cy - ay, vz = cz - az;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    outLen = std::sqrt(nx * nx + ny * ny + nz * nz);
    return nz;
  };

  std::vector<Projected3D> proj;
  for (const auto& spec : specs) {
    Surface* s = &tract->surfaces[spec.index];
    int nR = s->numRibs;
    int nP = s->numRibPoints;
    proj.assign(nR * nP, Projected3D{});
    for (int i = 0; i < nR; ++i) {
      for (int j = 0; j < nP; ++j) {
        proj[i * nP + j] =
            project3D(cam, s->getVertex(i, j), modelCenter, center, focal);
      }
    }
    // Push one triangle with the given vertex order. Computes per-face
    // shading and a depth key biased so back-facing tris (camera looks
    // along -z, so nz > 0) sort with the "far" group. Combined with
    // descending sort this gives back-faces (far→near), then
    // front-faces (far→near) — each surface's back face draws first,
    // its front face overdraws — which both fixes per-surface winding
    // ambiguity and suppresses cross-surface interleaving.
    auto pushTri = [&](const Projected3D& a, const Projected3D& b,
                       const Projected3D& c) {
      float nLen = 1.0f;
      float nz = faceNormalZ(a.camX, a.camY, a.camZ,
                             b.camX, b.camY, b.camZ,
                             c.camX, c.camY, c.camZ, nLen);
      float ndotv = (nLen > 1e-9f) ? std::abs(nz) / nLen : 0.0f;
      float bright = 0.4f + 0.6f * ndotv;
      Tri t;
      t.a = a.screen;
      t.b = b.screen;
      t.c = c.screen;
      float avgZ = (a.camZ + b.camZ + c.camZ) * (1.0f / 3.0f);
      constexpr float kBackFaceBias = 1.0e6f;
      t.depth = avgZ + (nz > 0.0f ? kBackFaceBias : 0.0f);
      t.color = shade(spec.color, bright);
      tris.push_back(t);
    };

    // Emit each surface triangle with both windings so the mesh is
    // two-sided regardless of the source surface's winding choice.
    auto emit = [&](const Projected3D& a, const Projected3D& b,
                    const Projected3D& c) {
      if (!a.visible || !b.visible || !c.visible) return;
      pushTri(a, b, c);
      pushTri(a, c, b);
    };
    for (int i = 0; i + 1 < nR; ++i) {
      for (int j = 0; j + 1 < nP; ++j) {
        const auto& p00 = proj[(i)     * nP + (j)];
        const auto& p10 = proj[(i + 1) * nP + (j)];
        const auto& p11 = proj[(i + 1) * nP + (j + 1)];
        const auto& p01 = proj[(i)     * nP + (j + 1)];
        emit(p00, p10, p11);
        emit(p00, p11, p01);
      }
    }
  }

  // Painter's algorithm: far first. Stable_sort keeps neighbouring
  // co-planar tris in submission order so adjacent triangles within
  // a surface don't flicker frame-to-frame.
  std::stable_sort(tris.begin(), tris.end(),
                   [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
  for (const auto& t : tris) {
    dl->AddTriangleFilled(t.a, t.b, t.c, t.color);
  }
}

// Wireframe variant: same surface set as the solid mesh, drawn as
// rib + rib-point lines with depth-based fade so back edges recede
// into the panel background.
void drawWireframe(ImDrawList* dl, VocalTract* tract, const Camera3D& cam,
                   Point3D modelCenter, ImVec2 canvasMin, ImVec2 canvasMax,
                   ImU32 bgColor) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW < 4.0f || canvasH < 4.0f) return;

  ImVec2 center((canvasMin.x + canvasMax.x) * 0.5f,
                (canvasMin.y + canvasMax.y) * 0.5f);
  float focal = std::min(canvasW, canvasH) * 0.9f;

  struct WireSpec {
    int index;
    ImU32 color;
  };
  const WireSpec specs[] = {
      {VocalTract::UPPER_COVER_TWOSIDE, IM_COL32(220, 180,  30, 255)},
      {VocalTract::LOWER_COVER_TWOSIDE, IM_COL32(180, 150,  30, 255)},
      {VocalTract::EPIGLOTTIS_TWOSIDE,  IM_COL32(180, 130,  30, 255)},
      {VocalTract::UVULA_TWOSIDE,       IM_COL32(220, 130,  30, 255)},
      {VocalTract::UPPER_TEETH_TWOSIDE, IM_COL32(240, 240, 240, 255)},
      {VocalTract::LOWER_TEETH_TWOSIDE, IM_COL32(240, 240, 240, 255)},
      {VocalTract::UPPER_LIP_TWOSIDE,   IM_COL32(220,  80,  60, 255)},
      {VocalTract::LOWER_LIP_TWOSIDE,   IM_COL32(220,  80,  60, 255)},
      {VocalTract::TONGUE,              IM_COL32(220,  80,  60, 255)},
  };

  // First pass: depth range so the fade has a meaningful t.
  float zNear = std::numeric_limits<float>::infinity();
  float zFar = -std::numeric_limits<float>::infinity();
  for (const auto& spec : specs) {
    Surface* s = &tract->surfaces[spec.index];
    for (int i = 0; i < s->numRibs; ++i) {
      for (int j = 0; j < s->numRibPoints; ++j) {
        Projected3D pj =
            project3D(cam, s->getVertex(i, j), modelCenter, center, focal);
        if (!pj.visible) continue;
        if (pj.camZ < zNear) zNear = pj.camZ;
        if (pj.camZ > zFar) zFar = pj.camZ;
      }
    }
  }
  float zSpan = zFar - zNear;
  if (zSpan < 1e-3f) zSpan = 1.0f;

  auto drawStrip = [&](Surface* s, ImU32 color, bool alongRibPoints) {
    int outerN = alongRibPoints ? s->numRibs : s->numRibPoints;
    int innerN = alongRibPoints ? s->numRibPoints : s->numRibs;
    std::vector<ImVec2> pts;
    std::vector<float> depths;
    for (int o = 0; o < outerN; ++o) {
      pts.clear();
      depths.clear();
      for (int i = 0; i < innerN; ++i) {
        Point3D p = alongRibPoints ? s->getVertex(o, i) : s->getVertex(i, o);
        Projected3D pj = project3D(cam, p, modelCenter, center, focal);
        if (!pj.visible) {
          for (size_t j = 1; j < pts.size(); ++j) {
            float t = ((depths[j - 1] + depths[j]) * 0.5f - zNear) / zSpan;
            dl->AddLine(pts[j - 1], pts[j], fadeColor(color, bgColor, t),
                        1.0f);
          }
          pts.clear();
          depths.clear();
          continue;
        }
        pts.push_back(pj.screen);
        depths.push_back(pj.camZ);
      }
      for (size_t j = 1; j < pts.size(); ++j) {
        float t = ((depths[j - 1] + depths[j]) * 0.5f - zNear) / zSpan;
        dl->AddLine(pts[j - 1], pts[j], fadeColor(color, bgColor, t), 1.0f);
      }
    }
  };
  for (const auto& spec : specs) {
    Surface* s = &tract->surfaces[spec.index];
    drawStrip(s, spec.color, true);
    drawStrip(s, spec.color, false);
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

  // Use the articulation-extent bounds for both the orbit pivot and
  // the auto-fit camera distance. This keeps the model framed
  // consistently across slider movements: the centroid doesn't drift
  // and the initial zoom doesn't shrink when articulation grows the
  // live bbox.
  const TractBounds& bounds = articulationExtentBounds(tract);
  Point3D modelCenter;
  modelCenter.x = bounds.cx();
  modelCenter.y = bounds.cy();
  modelCenter.z = bounds.cz();
  if (cam.distance_cm <= 0.0f) {
    // Distance such that the largest bbox dimension projects to ~70%
    // of the smaller canvas axis (focal = 0.9 * minDim, so distance =
    // focal * extent / desiredScreenSpan ≈ 1.3 * extent).
    cam.distance_cm = (float)(bounds.maxExtent() * 1.3);
    if (cam.distance_cm < 8.0f) cam.distance_cm = 8.0f;
  }

  static RenderMode mode = RenderMode::Solid;
  if (mode == RenderMode::Solid) {
    drawSolidMesh(dl, tract, cam, modelCenter, canvasMin, canvasMax, bgColor);
  } else {
    drawWireframe(dl, tract, cam, modelCenter, canvasMin, canvasMax, bgColor);
  }
  dl->AddRect(canvasMin, canvasMax, ImGui::GetColorU32(ImGuiCol_Border));

  // Submit the toolbar BEFORE the canvas-spanning InvisibleButton so
  // the toolbar widgets get hover/click priority. Otherwise the
  // InvisibleButton (which covers the entire panel) would claim
  // every click and the segmented toggle would never fire.
  // Segmented Solid/Wire toggle in the top-right corner. Two
  // SmallButtons sit flush with 2 px between them; the active one is
  // tinted ButtonActive so it reads as pressed. Stand-in for ImGui's
  // missing native segmented control.
  auto modeBtn = [&](const char* label, RenderMode m) {
    bool selected = (mode == m);
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetColorU32(ImGuiCol_ButtonActive));
    }
    if (ImGui::SmallButton(label)) mode = m;
    if (selected) ImGui::PopStyleColor();
  };
  // Right-align: estimate the toolbar's width from button label sizes
  // + frame padding so the cursor lands precisely at the panel edge.
  ImGuiStyle& style = ImGui::GetStyle();
  float padX2 = style.FramePadding.x * 2.0f;
  float wSolid = ImGui::CalcTextSize("solid").x + padX2;
  float wWire = ImGui::CalcTextSize("wire").x + padX2;
  float wReset = ImGui::CalcTextSize("reset").x + padX2;
  float btnSpacing = 2.0f;
  float groupGap = 8.0f;
  float toolbarW = wSolid + btnSpacing + wWire + groupGap + wReset;
  const float pad = 6.0f;
  ImGui::SetCursorScreenPos(
      ImVec2(canvasMax.x - pad - toolbarW, canvasMin.y + pad));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(btnSpacing, 0.0f));
  modeBtn("solid##mode3d", RenderMode::Solid);
  ImGui::SameLine();
  modeBtn("wire##mode3d", RenderMode::Wireframe);
  ImGui::PopStyleVar();
  ImGui::SameLine(0.0f, groupGap);
  if (ImGui::SmallButton("reset##cam3d")) {
    // Sentinel so the auto-fit branch re-runs next frame against
    // the current articulation.
    cam = Camera3D{};
  }

  // Canvas-spanning InvisibleButton for orbit drag + wheel zoom.
  // Submitted after the toolbar so toolbar clicks aren't stolen by
  // the canvas hit region.
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

  ImGui::End();
}

}  // namespace live
