// ****************************************************************************
// VocalTractLab realtime experimentation lab.
//
// ImGui-based companion app to the wxWidgets GUI. Streams audio through the
// vtl synthesizer in real time and lets the user move the fundamental, the
// subglottal pressure, and the articulation parameters around with sliders
// while watching the vocal tract outline and the radiated spectrum update.
// ****************************************************************************

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "AudioEngine.h"
#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Constants.h"
#include "core/Geometry.h"
#include "dsp/Dsp.h"
#include "dsp/Signal.h"
#include "glottis/Glottis.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GLFW/glfw3.h>

namespace {

// ---- Asset discovery ------------------------------------------------------
//
// We try the executable's directory first (Linux/Windows install), then the
// macOS bundle's Resources/, and finally the source tree (so a binary built
// in build/<preset>/ during development can still find the speaker file).

std::filesystem::path findSpeakerFile(const char* argv0) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path exe(argv0);
  fs::path exeDir = fs::absolute(exe, ec).parent_path();

  std::vector<fs::path> candidates;
  candidates.push_back(exeDir / "JD2.speaker");
#if defined(__APPLE__)
  // For .app bundles: argv0 is .../Contents/MacOS/vtl_live.
  candidates.push_back(exeDir.parent_path() / "Resources" / "JD2.speaker");
#endif
  candidates.push_back(exeDir / ".." / "JD2.speaker");
  candidates.push_back(exeDir / ".." / "data" / "speakers" / "JD2.speaker");
  candidates.push_back(exeDir / ".." / ".." / "data" / "speakers" /
                       "JD2.speaker");
  candidates.push_back(exeDir / ".." / ".." / ".." / "data" / "speakers" /
                       "JD2.speaker");

  for (const auto& p : candidates) {
    if (fs::exists(p, ec)) return fs::canonical(p, ec);
  }
  return fs::path();
}

// ---- Vocal tract drawing --------------------------------------------------
//
// Pulls the same line strips that VocalTractPicture::render2D draws on the
// wxGLCanvas — upper and lower covers (with uvula / epiglottis insertions),
// the tongue mediosagittal contour, the dashed tongue circle, the teeth and
// lips. Coordinates from the surfaces are in cm; the helper maps them into
// the supplied screen rectangle preserving aspect ratio.

struct TractView {
  ImVec2 origin;     // screen pixel that maps to (0, 0) in cm
  float scale_px_per_cm;
  ImVec2 toScreen(double x_cm, double y_cm) const {
    return ImVec2(origin.x + (float)x_cm * scale_px_per_cm,
                  origin.y - (float)y_cm * scale_px_per_cm);
  }
  ImVec2 toModel(ImVec2 screen) const {
    return ImVec2((screen.x - origin.x) / scale_px_per_cm,
                  (origin.y - screen.y) / scale_px_per_cm);
  }
};

TractView computeTractView(ImVec2 canvasMin, ImVec2 canvasMax) {
  const double modelW = 22.0;
  const double modelH = 16.0;
  const double modelCx = -2.0;
  const double modelCy = -4.0;
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  float scale = std::min(canvasW / (float)modelW, canvasH / (float)modelH);
  TractView view;
  view.scale_px_per_cm = scale;
  view.origin = ImVec2(canvasMin.x + canvasW * 0.5f - (float)modelCx * scale,
                       canvasMin.y + canvasH * 0.5f + (float)modelCy * scale);
  return view;
}

// ---- Interactive control points ------------------------------------------
//
// Mirrors the wxGLCanvas-based VocalTractPicture::setControlPoint /
// parameterToControlPoint pair: each handle reads/writes a small set of
// vocal tract parameters, then restrictParam / restrictTongueParams clamps
// the result back into the model's allowed range.

enum ControlPointKind {
  CP_JAW,
  CP_LIP_CORNER,
  CP_LIP_DISTANCE,
  CP_TONGUE_CENTER,
  CP_TONGUE_TIP,
  CP_TONGUE_BLADE,
  CP_TONGUE_BACK,
  CP_VELUM,
  CP_HYOID,
  CP_COUNT
};

struct ControlPointInfo {
  ImVec2 modelPos;  // (cm, cm) in mediosagittal plane
};

const char* controlPointLabel(int kind) {
  switch (kind) {
    case CP_JAW: return "jaw";
    case CP_LIP_CORNER: return "lip corner";
    case CP_LIP_DISTANCE: return "lip distance";
    case CP_TONGUE_CENTER: return "tongue center";
    case CP_TONGUE_TIP: return "tongue tip";
    case CP_TONGUE_BLADE: return "tongue blade";
    case CP_TONGUE_BACK: return "tongue back";
    case CP_VELUM: return "velum";
    case CP_HYOID: return "hyoid";
    default: return "?";
  }
}

ControlPointInfo getControlPoint(VocalTract* tract, int kind) {
  ControlPointInfo cp{};
  switch (kind) {
    case CP_JAW: {
      Surface* s = &tract->surfaces[VocalTract::LOWER_COVER];
      int rib = VocalTract::NUM_LARYNX_RIBS + VocalTract::NUM_THROAT_RIBS;
      Point3D q = s->getVertex(rib, 0);
      cp.modelPos = ImVec2((float)q.x, (float)q.y);
    } break;
    case CP_LIP_CORNER: {
      Point3D onset, corner, F0p, F1p;
      double yClose;
      tract->getImportantLipPoints(onset, corner, F0p, F1p, yClose);
      cp.modelPos = ImVec2((float)corner.x, (float)corner.y);
    } break;
    case CP_LIP_DISTANCE: {
      Point3D onset, corner, F0p, F1p;
      double yClose;
      tract->getImportantLipPoints(onset, corner, F0p, F1p, yClose);
      double y = yClose - 0.5 * tract->params[VocalTract::LD].x -
                 tract->params[VocalTract::JX].x;
      cp.modelPos = ImVec2((float)F1p.x, (float)y);
    } break;
    case CP_TONGUE_CENTER:
      cp.modelPos = ImVec2((float)tract->params[VocalTract::TCX].x,
                           (float)tract->params[VocalTract::TCY].x);
      break;
    case CP_TONGUE_TIP:
      cp.modelPos = ImVec2((float)tract->params[VocalTract::TTX].x,
                           (float)tract->params[VocalTract::TTY].x);
      break;
    case CP_TONGUE_BLADE:
      cp.modelPos = ImVec2((float)tract->params[VocalTract::TBX].x,
                           (float)tract->params[VocalTract::TBY].x);
      break;
    case CP_TONGUE_BACK:
      cp.modelPos = ImVec2((float)tract->params[VocalTract::TRX].x,
                           (float)tract->params[VocalTract::TRY].x);
      break;
    case CP_VELUM: {
      // Maps onto a virtual 2x1 cm rectangle anchored above the velum.
      auto& vo = tract->params[VocalTract::VO];
      auto& vs = tract->params[VocalTract::VS];
      double t1 =
          (vo.max > vo.min) ? (vo.x - vo.min) / (vo.max - vo.min) : 0.0;
      double t2 =
          (vs.max > vs.min) ? (vs.x - vs.min) / (vs.max - vs.min) : 0.0;
      double x = -2.5 + t1 * (-1.5 - -2.5);
      double y = 1.0 + t2 * (0.0 - 1.0);
      cp.modelPos = ImVec2((float)x, (float)y);
    } break;
    case CP_HYOID: {
      Surface* s = &tract->surfaces[VocalTract::LOWER_COVER];
      int rib = VocalTract::NUM_LARYNX_RIBS - 1;
      int rp = VocalTract::NUM_LOWER_COVER_POINTS - 1;
      Point3D q = s->getVertex(rib, rp);
      cp.modelPos = ImVec2((float)q.x, (float)q.y);
    } break;
  }
  return cp;
}

// Project the target point onto a polyline; returns arc-length-normalized
// parameter in [0, 1].
double projectOntoPath3D(const Point3D* path, int n, double tx, double ty) {
  if (n < 2) return 0.0;
  std::vector<double> seg((size_t)n, 0.0);
  for (int i = 1; i < n; ++i) {
    double dx = path[i].x - path[i - 1].x;
    double dy = path[i].y - path[i - 1].y;
    seg[i] = seg[i - 1] + std::sqrt(dx * dx + dy * dy);
  }
  double total = seg.back();
  if (total < 1e-6) return 0.0;
  double bestD = std::numeric_limits<double>::infinity();
  double bestS = 0.0;
  for (int i = 0; i < n - 1; ++i) {
    double dx = path[i + 1].x - path[i].x;
    double dy = path[i + 1].y - path[i].y;
    double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 1e-9) {
      t = ((tx - path[i].x) * dx + (ty - path[i].y) * dy) / len2;
    }
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double px = path[i].x + t * dx;
    double py = path[i].y + t * dy;
    double dd = (px - tx) * (px - tx) + (py - ty) * (py - ty);
    if (dd < bestD) {
      bestD = dd;
      bestS = (seg[i] + t * (seg[i + 1] - seg[i])) / total;
    }
  }
  return bestS;
}

void applyControlPointDrag(VocalTract* tract, double* params, int kind,
                           ImVec2 target) {
  auto write = [&](int idx, double v) {
    tract->params[idx].x = v;
    tract->restrictParam(idx);
    params[idx] = tract->params[idx].x;
  };
  switch (kind) {
    case CP_TONGUE_CENTER:
      tract->params[VocalTract::TCX].x = target.x;
      tract->params[VocalTract::TCY].x = target.y;
      tract->restrictTongueParams();
      params[VocalTract::TCX] = tract->params[VocalTract::TCX].x;
      params[VocalTract::TCY] = tract->params[VocalTract::TCY].x;
      break;
    case CP_TONGUE_TIP:
      tract->params[VocalTract::TTX].x = target.x;
      tract->params[VocalTract::TTY].x = target.y;
      tract->restrictTongueParams();
      params[VocalTract::TTX] = tract->params[VocalTract::TTX].x;
      params[VocalTract::TTY] = tract->params[VocalTract::TTY].x;
      break;
    case CP_TONGUE_BLADE:
      tract->params[VocalTract::TBX].x = target.x;
      tract->params[VocalTract::TBY].x = target.y;
      tract->restrictTongueParams();
      params[VocalTract::TBX] = tract->params[VocalTract::TBX].x;
      params[VocalTract::TBY] = tract->params[VocalTract::TBY].x;
      break;
    case CP_TONGUE_BACK:
      tract->params[VocalTract::TRX].x = target.x;
      tract->params[VocalTract::TRY].x = target.y;
      tract->restrictTongueParams();
      params[VocalTract::TRX] = tract->params[VocalTract::TRX].x;
      params[VocalTract::TRY] = tract->params[VocalTract::TRY].x;
      break;
    case CP_LIP_DISTANCE: {
      Point3D onset, corner, F0p, F1p;
      double yClose;
      tract->getImportantLipPoints(onset, corner, F0p, F1p, yClose);
      double newLD =
          -2.0 * ((double)target.y - yClose + tract->params[VocalTract::JX].x);
      write(VocalTract::LD, newLD);
    } break;
    case CP_LIP_CORNER: {
      // Map drop point to closest position along the lip-corner path.
      int N = tract->lipCornerPath.getNumPoints();
      if (N < 2) break;
      Point3D pts[MAX_SPLINE_POINTS];
      for (int i = 0; i < N; ++i) {
        pts[i] = tract->lipCornerPath.getControlPoint(i);
      }
      double s = projectOntoPath3D(pts, N, target.x, target.y);
      auto& lp = tract->params[VocalTract::LP];
      write(VocalTract::LP, lp.min + s * (lp.max - lp.min));
    } break;
    case CP_JAW: {
      Point2D v = tract->anatomy.jawRestPos - tract->anatomy.jawFulcrum;
      Point2D w =
          Point2D(target.x, target.y) - tract->anatomy.jawFulcrum;
      double denom = w.y * w.y + w.x * w.x;
      if (denom < 1e-6) denom = 1e-6;
      double pp = 2.0 * v.y * w.x / denom;
      double qq = (v.y * v.y - w.y * w.y) / denom;
      double radicand = 0.25 * pp * pp - qq;
      if (radicand >= 0.0) {
        double S2 = -0.5 * pp - std::sqrt(radicand);
        if (S2 > -1.0 && S2 < 1.0) {
          double angle_rad = std::asin(S2);
          double newJA = angle_rad * 180.0 / M_PI;
          double newJX = tract->params[VocalTract::JX].x;
          if (angle_rad > -M_PI / 4.0 && angle_rad < M_PI / 4.0) {
            newJX = (w.x + std::sin(angle_rad) * v.y) / std::cos(angle_rad) -
                    v.x;
          }
          write(VocalTract::JX, newJX);
          write(VocalTract::JA, newJA);
        }
      }
    } break;
    case CP_HYOID: {
      Surface* sNarrow = &tract->surfaces[VocalTract::NARROW_LARYNX_FRONT];
      Surface* sWide = &tract->surfaces[VocalTract::WIDE_LARYNX_FRONT];
      int rib = VocalTract::NUM_LARYNX_RIBS - 1;
      int rp = VocalTract::NUM_LOWER_COVER_POINTS - 1;
      Point3D narrowP = sNarrow->getVertex(rib, rp);
      Point3D wideP = sWide->getVertex(rib, rp);
      double hyoidY =
          tract->surfaces[VocalTract::LOWER_COVER].getVertex(rib, rp).y;
      double xRel = (double)target.x - tract->getPharynxBackX(hyoidY);
      double denomHX = wideP.x - narrowP.x;
      if (std::fabs(denomHX) < 1e-6) denomHX = 1e-6;
      double t = (xRel - narrowP.x) / denomHX;
      write(VocalTract::HX, t);
      write(VocalTract::HY, target.y);
    } break;
    case CP_VELUM: {
      auto& vo = tract->params[VocalTract::VO];
      auto& vs = tract->params[VocalTract::VS];
      double xLeft = -2.5, xRight = -1.5, yTop = 1.0, yBottom = 0.0;
      double t1 = ((double)target.x - xLeft) / (xRight - xLeft);
      double t2 = ((double)target.y - yTop) / (yBottom - yTop);
      write(VocalTract::VO, vo.min + t1 * (vo.max - vo.min));
      write(VocalTract::VS, vs.min + t2 * (vs.max - vs.min));
    } break;
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

void drawVocalTract(ImDrawList* dl, VocalTract* tract,
                    const TractView& view) {
  const ImU32 colBlack = IM_COL32(20, 20, 20, 255);
  const ImU32 colTongue = IM_COL32(220, 80, 60, 255);
  const ImU32 colTongueSide = IM_COL32(200, 200, 200, 255);
  const ImU32 colDashed = IM_COL32(120, 120, 120, 255);
  const ImU32 colLip = IM_COL32(220, 80, 60, 255);
  const float thickThin = 1.0f;
  const float thickMain = 1.6f;

  std::vector<ImVec2> pts;

  // ---- Upper cover (with uvula inserted at its centerline) ----
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
  drawStrip(dl, pts, colBlack, thickMain);

  // ---- Lower cover with epiglottis inserted ----
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
  drawStrip(dl, pts, colBlack, thickMain);

  // ---- Tongue mediosagittal contour ----
  pts.clear();
  s = &tract->surfaces[VocalTract::TONGUE];
  ribPoint = s->numRibPoints / 2;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colTongue, thickMain);

  // ---- Tongue 1 cm side line (intersects z = -1 cm with each tongue rib) ----
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

  // ---- Dashed tongue body ellipse ----
  {
    double mx = tract->params[VocalTract::TCX].limitedX;
    double my = tract->params[VocalTract::TCY].limitedX;
    double rx = tract->anatomy.tongueCenterRadiusX_cm;
    double ry = tract->anatomy.tongueCenterRadiusY_cm;
    constexpr int N = 64;
    for (int i = 0; i < N; ++i) {
      // Gap every other segment to fake a dashed line.
      if ((i & 1) == 0) continue;
      double a0 = 2.0 * M_PI * (double)i / (double)N;
      double a1 = 2.0 * M_PI * (double)(i + 1) / (double)N;
      ImVec2 p0 = view.toScreen(mx + rx * std::cos(a0),
                                my + ry * std::sin(a0));
      ImVec2 p1 = view.toScreen(mx + rx * std::cos(a1),
                                my + ry * std::sin(a1));
      dl->AddLine(p0, p1, colDashed, thickThin);
    }
  }

  // ---- Upper teeth: inner edge + cropped outer edge + first/last full rib ----
  s = &tract->surfaces[VocalTract::UPPER_TEETH];
  pts.clear();
  ribPoint = 0;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  ribPoint = 2;
  for (int i = 0; i < s->numRibs - 4; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < 2; ++i) {
    Point3D q = s->getVertex(0, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 2, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  // ---- Lower teeth ----
  s = &tract->surfaces[VocalTract::LOWER_TEETH];
  pts.clear();
  ribPoint = 0;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  ribPoint = 2;
  for (int i = 0; i < s->numRibs - 7; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < 2; ++i) {
    Point3D q = s->getVertex(0, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 2, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  // ---- Lips ----
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

// ---- 3D wireframe drawing -------------------------------------------------
//
// Software perspective projection over ImDrawList — the same surfaces the
// wxGLCanvas wireframe view paints (upper/lower covers + teeth + lips +
// uvula + epiglottis + tongue), but rendered in pure 2D drawlist segments
// so we don't need a separate FBO. The mouse drives a yaw/pitch/distance
// orbit camera; depth-fade darkens far-side lines so the model reads
// correctly without a true z-buffer.

struct Camera3D {
  float yaw_deg = 25.0f;
  float pitch_deg = 12.0f;
  float distance_cm = 30.0f;  // > 0; smaller = bigger model on screen
};

struct Projected3D {
  ImVec2 screen;
  float depth;     // distance from camera, cm; bigger = farther
  bool visible;    // false if behind near plane
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
  // Rotate around Y (yaw), then X (pitch).
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

ImU32 fadeColor(ImU32 base, float t) {
  // t = 0 (near) → full color, t = 1 (far) → blend toward background gray.
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float keep = 1.0f - 0.7f * t;
  int r = (int)(((base >> IM_COL32_R_SHIFT) & 0xFF) * keep +
                255.0f * (1 - keep) * 0.93f);
  int g = (int)(((base >> IM_COL32_G_SHIFT) & 0xFF) * keep +
                255.0f * (1 - keep) * 0.93f);
  int b = (int)(((base >> IM_COL32_B_SHIFT) & 0xFF) * keep +
                255.0f * (1 - keep) * 0.95f);
  return IM_COL32(r, g, b, 255);
}

void draw3DTract(ImDrawList* dl, VocalTract* tract, const Camera3D& cam,
                 ImVec2 canvasMin, ImVec2 canvasMax) {
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
  auto drawSurfaceStrip = [&](Surface* s, ImU32 color, bool ribsAlongRibPoints) {
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
          // Flush any partial strip and start a new one after the gap.
          if (pts.size() >= 2) {
            for (size_t j = 1; j < pts.size(); ++j) {
              float t = ((depths[j - 1] + depths[j]) * 0.5f - zNear) / zSpan;
              dl->AddLine(pts[j - 1], pts[j], fadeColor(color, t), 1.0f);
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
        dl->AddLine(pts[j - 1], pts[j], fadeColor(color, t), 1.0f);
      }
    }
  };

  for (const auto& spec : specs) {
    Surface* s = &tract->surfaces[spec.index];
    drawSurfaceStrip(s, spec.color, true);   // along ribs
    drawSurfaceStrip(s, spec.color, false);  // along rib points
  }
}

// ---- Spectrum drawing -----------------------------------------------------

constexpr int FFT_LEN_EXPONENT = 10;
constexpr int FFT_LEN = 1 << FFT_LEN_EXPONENT;  // 1024 samples ≈ 21 ms

void drawSpectrum(ImDrawList* dl, live::AudioHistory& history,
                  ComplexSignal& fftBuf, ImVec2 canvasMin, ImVec2 canvasMax) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW <= 1.0f || canvasH <= 1.0f) return;

  // Background and frame.
  const ImU32 colBg = IM_COL32(245, 245, 245, 255);
  const ImU32 colGrid = IM_COL32(200, 200, 200, 255);
  const ImU32 colCurve = IM_COL32(40, 80, 200, 255);
  const ImU32 colText = IM_COL32(80, 80, 80, 255);
  dl->AddRectFilled(canvasMin, canvasMax, colBg);

  // Pull latest samples and apply Hann window.
  static std::array<float, FFT_LEN> samples;
  history.copyLatest(samples.data(), FFT_LEN);

  fftBuf.reset(FFT_LEN);
  for (int i = 0; i < FFT_LEN; ++i) {
    double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (double)(FFT_LEN - 1));
    fftBuf.re[i] = (double)samples[i] * w;
    fftBuf.im[i] = 0.0;
  }
  complexFFT(fftBuf, FFT_LEN_EXPONENT, true);

  // Frequency / dB axes.
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

  // Vertical grid every 1 kHz with labels.
  for (int khz = 1; khz < (int)(fMax_Hz / 1000.0); ++khz) {
    float x = xForFreq((double)khz * 1000.0);
    dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%dk", khz);
    dl->AddText(ImVec2(x + 2.0f, canvasMax.y - 14.0f), colText, label);
  }
  // Horizontal grid every 20 dB with labels.
  for (double db = dbMin + 20.0; db < dbMax; db += 20.0) {
    float y = yForDb(db);
    dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%d dB", (int)db);
    dl->AddText(ImVec2(canvasMin.x + 2.0f, y - 14.0f), colText, label);
  }

  // Spectrum curve: one polyline along the visible canvas, sampling the FFT
  // bins at each pixel. Magnitudes get a small floor before the log to keep
  // the curve numerically sane near silence.
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
    pts.push_back(
        ImVec2(canvasMin.x + (float)px, yForDb(db)));
  }
  if (pts.size() >= 2) {
    dl->AddPolyline(pts.data(), (int)pts.size(), colCurve, ImDrawFlags_None,
                    1.5f);
  }
  dl->AddRect(canvasMin, canvasMax, IM_COL32(150, 150, 150, 255));
}

// ---- GLFW glue ------------------------------------------------------------

void glfwErrorCallback(int code, const char* msg) {
  std::fprintf(stderr, "glfw error %d: %s\n", code, msg);
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path speakerPath =
      findSpeakerFile(argc > 0 ? argv[0] : "vtl_live");
  if (speakerPath.empty()) {
    std::fprintf(stderr,
                 "live: could not locate JD2.speaker next to the binary\n");
    return 1;
  }

  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) return 1;

#if defined(__APPLE__)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  const char* glslVersion = "#version 150";
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  const char* glslVersion = "#version 130";
#endif

  GLFWwindow* window =
      glfwCreateWindow(1280, 800, "VocalTractLab Live", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsLight();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);

  live::AudioEngine engine;
  if (!engine.start(speakerPath.string())) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  // FFT scratch space, reused each frame.
  ComplexSignal fftBuf(FFT_LEN);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Full-viewport dockspace. The first frame builds a default layout
    // (controls left, vocal tract upper-right, spectrum lower-right);
    // subsequent runs honour whatever the user dragged the panels into,
    // since ImGui persists dock state to imgui.ini.
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
        0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    static bool dockBuilt = false;
    if (!dockBuilt) {
      dockBuilt = true;
      ImGui::DockBuilderRemoveNode(dockspace_id);
      ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspace_id,
                                    ImGui::GetMainViewport()->Size);
      ImGuiID leftId, rightId;
      ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.32f, &leftId,
                                  &rightId);
      ImGuiID rightTopId, rightBottomId;
      ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.65f, &rightTopId,
                                  &rightBottomId);
      ImGui::DockBuilderDockWindow("Controls", leftId);
      ImGui::DockBuilderDockWindow("Vocal Tract", rightTopId);
      ImGui::DockBuilderDockWindow("Vocal Tract 3D", rightTopId);
      ImGui::DockBuilderDockWindow("Primary Spectrum", rightBottomId);
      ImGui::DockBuilderFinish(dockspace_id);
    }

    // Snapshot of control state for this frame's UI; written-back at the end.
    double f0_Hz, pressure_dPa;
    float gain;
    std::vector<double> tractParamsLocal(VocalTract::NUM_PARAMS);
    std::vector<double> glottisParamsLocal(engine.numGlottisParams());
    {
      std::lock_guard<std::mutex> lk(engine.control.mtx);
      f0_Hz = engine.control.f0_Hz;
      pressure_dPa = engine.control.pressure_dPa;
      gain = engine.control.outputGain;
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        tractParamsLocal[i] = engine.control.tractParams[i];
      }
      for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
        glottisParamsLocal[i] = engine.control.glottisParams[i];
      }
    }

    // -------- Controls --------
    ImGui::Begin("Controls");

    {
      float f0_f = (float)f0_Hz;
      float p_f = (float)pressure_dPa;
      ImGui::TextUnformatted("Source");
      ImGui::Separator();
      if (ImGui::SliderFloat("F0 (Hz)", &f0_f, 50.0f, 500.0f, "%.1f")) {
        f0_Hz = f0_f;
      }
      if (ImGui::SliderFloat("Subglottal pressure (dPa)", &p_f, 0.0f, 16000.0f,
                             "%.0f")) {
        pressure_dPa = p_f;
      }
      ImGui::SliderFloat("Output gain", &gain, 0.0f, 1.0f, "%.2f");

      ImGui::Spacing();
      ImGui::TextUnformatted("Vowel preset");
      const auto& shapes = engine.tractShapes();
      static int comboIndex = -1;
      const char* preview =
          (comboIndex >= 0 && comboIndex < (int)shapes.size())
              ? shapes[comboIndex].name.c_str()
              : "(custom)";
      if (ImGui::BeginCombo("Tract shape", preview)) {
        for (int i = 0; i < (int)shapes.size(); ++i) {
          bool selected = (i == comboIndex);
          if (ImGui::Selectable(shapes[i].name.c_str(), selected)) {
            comboIndex = i;
            for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
              tractParamsLocal[p] = shapes[i].param[p];
            }
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Articulation",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        const auto& info = engine.tractParamInfo(i);
        float v = (float)tractParamsLocal[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%s##t%d", info.abbr.c_str(), i);
        if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                               "%.2f")) {
          tractParamsLocal[i] = v;
        }
      }
    }

    if (ImGui::CollapsingHeader("Glottis", ImGuiTreeNodeFlags_DefaultOpen)) {
      const auto& gShapes = engine.glottisShapes();
      static int gComboIndex = -1;
      const char* gPreview =
          (gComboIndex >= 0 && gComboIndex < (int)gShapes.size())
              ? gShapes[gComboIndex].name.c_str()
              : "(custom)";
      if (ImGui::BeginCombo("Glottis shape", gPreview)) {
        for (int i = 0; i < (int)gShapes.size(); ++i) {
          bool selected = (i == gComboIndex);
          if (ImGui::Selectable(gShapes[i].name.c_str(), selected)) {
            gComboIndex = i;
            int n = std::min((int)gShapes[i].controlParam.size(),
                             (int)glottisParamsLocal.size());
            for (int p = 0; p < n; ++p) {
              if (p == Glottis::FREQUENCY || p == Glottis::PRESSURE) continue;
              glottisParamsLocal[p] = gShapes[i].controlParam[p];
            }
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
        if (i == Glottis::FREQUENCY || i == Glottis::PRESSURE) continue;
        const auto& info = engine.glottisParamInfo(i);
        float v = (float)glottisParamsLocal[i];
        char label[80];
        std::snprintf(label, sizeof(label), "%s##g%d", info.abbr.c_str(), i);
        if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                               "%.3f")) {
          glottisParamsLocal[i] = v;
        }
      }
    }

    ImGui::End();

    // -------- Vocal tract --------
    ImGui::Begin("Vocal Tract");
    {
      // Apply current articulation to the visualization tract and rebuild.
      VocalTract* tract = engine.uiTract();
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        tract->params[i].x = tractParamsLocal[i];
      }
      tract->calculateAll();

      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 canvasMin = pos;
      ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(255, 255, 255, 255));
      TractView view = computeTractView(canvasMin, canvasMax);
      drawVocalTract(dl, tract, view);

      // InvisibleButton owns the drag interaction over the canvas.
      ImGui::SetCursorScreenPos(canvasMin);
      ImGui::InvisibleButton("##vt_canvas", avail,
                             ImGuiButtonFlags_MouseButtonLeft);
      bool itemActive = ImGui::IsItemActive();
      bool itemHovered = ImGui::IsItemHovered();

      static int draggingPoint = -1;
      int hoverPoint = -1;
      ImVec2 mousePx = ImGui::GetIO().MousePos;
      const float pickRadius_px = 9.0f;
      ControlPointInfo pts[CP_COUNT];
      bool dragging = (draggingPoint >= 0);
      for (int i = 0; i < CP_COUNT; ++i) {
        pts[i] = getControlPoint(tract, i);
        ImVec2 sp = view.toScreen(pts[i].modelPos.x, pts[i].modelPos.y);
        float dx = sp.x - mousePx.x, dy = sp.y - mousePx.y;
        if (itemHovered && !dragging &&
            dx * dx + dy * dy <= pickRadius_px * pickRadius_px &&
            hoverPoint < 0) {
          hoverPoint = i;
        }
      }

      if (itemHovered && ImGui::IsMouseClicked(0) && hoverPoint >= 0) {
        draggingPoint = hoverPoint;
      }
      if (!ImGui::IsMouseDown(0)) draggingPoint = -1;

      if (draggingPoint >= 0 && itemActive) {
        // Apply the same parameter mapping logic as VocalTractPicture.
        ImVec2 modelTarget = view.toModel(mousePx);
        applyControlPointDrag(tract, tractParamsLocal.data(), draggingPoint,
                              modelTarget);
        // Recompute geometry so this frame's overlay reflects the drag.
        for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
          tract->params[i].x = tractParamsLocal[i];
        }
        tract->calculateAll();
        for (int i = 0; i < CP_COUNT; ++i) pts[i] = getControlPoint(tract, i);
      }

      // Draw control point markers on top of the outline.
      for (int i = 0; i < CP_COUNT; ++i) {
        ImVec2 sp = view.toScreen(pts[i].modelPos.x, pts[i].modelPos.y);
        bool active = (draggingPoint == i);
        bool hover = (hoverPoint == i);
        ImU32 fill = active ? IM_COL32(40, 120, 220, 255)
                            : hover ? IM_COL32(255, 200, 80, 255)
                                    : IM_COL32(255, 255, 255, 255);
        ImU32 outline = IM_COL32(20, 20, 20, 255);
        dl->AddCircleFilled(sp, 5.0f, fill);
        dl->AddCircle(sp, 5.0f, outline, 0, 1.5f);
        if (hover || active) {
          dl->AddText(ImVec2(sp.x + 8.0f, sp.y - 8.0f), outline,
                      controlPointLabel(i));
        }
      }
      dl->AddRect(canvasMin, canvasMax, IM_COL32(150, 150, 150, 255));
    }
    ImGui::End();

    // -------- Vocal tract (3D wireframe) --------
    ImGui::Begin("Vocal Tract 3D");
    {
      static Camera3D cam;
      VocalTract* tract = engine.uiTract();
      // The 2D panel already pushed tractParamsLocal into tract->params and
      // called calculateAll() this frame, so the surfaces are up to date.

      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 canvasMin = pos;
      ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
      ImDrawList* dl3 = ImGui::GetWindowDrawList();
      dl3->AddRectFilled(canvasMin, canvasMax, IM_COL32(245, 245, 250, 255));
      draw3DTract(dl3, tract, cam, canvasMin, canvasMax);
      dl3->AddRect(canvasMin, canvasMax, IM_COL32(150, 150, 150, 255));

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
      // Bottom-left HUD with view info + reset button.
      ImGui::SetCursorScreenPos(
          ImVec2(canvasMin.x + 6.0f, canvasMax.y - 28.0f));
      ImGui::Text("yaw %.0f° pitch %.0f° dist %.0fcm  (drag to orbit, wheel "
                  "to zoom)",
                  cam.yaw_deg, cam.pitch_deg, cam.distance_cm);
      ImGui::SameLine();
      if (ImGui::SmallButton("reset##cam3d")) {
        cam = Camera3D{};
      }
    }
    ImGui::End();

    // -------- Spectrum --------
    ImGui::Begin("Primary Spectrum");
    {
      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 canvasMin = pos;
      ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
      drawSpectrum(ImGui::GetWindowDrawList(), engine.history, fftBuf,
                   canvasMin, canvasMax);
      ImGui::Dummy(avail);
    }
    ImGui::End();

    // Push UI state back to the audio thread.
    {
      std::lock_guard<std::mutex> lk(engine.control.mtx);
      engine.control.f0_Hz = f0_Hz;
      engine.control.pressure_dPa = pressure_dPa;
      engine.control.outputGain = gain;
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        engine.control.tractParams[i] = tractParamsLocal[i];
      }
      for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
        engine.control.glottisParams[i] = glottisParamsLocal[i];
      }
    }

    ImGui::Render();
    int displayW, displayH;
    glfwGetFramebufferSize(window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.93f, 0.93f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  engine.stop();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
