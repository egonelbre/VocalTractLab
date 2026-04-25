#include "ControlPoints.h"

#include <cmath>
#include <limits>
#include <vector>

#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Geometry.h"

namespace live {

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
      // Maps onto a virtual 1×1 cm rectangle anchored above the velum so the
      // user has somewhere to grab even when the velum is closed.
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

namespace {

// Project the target point onto a polyline in the xy-plane; returns the
// arc-length-normalized parameter in [0, 1].
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

}  // namespace

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
      Point2D w = Point2D(target.x, target.y) - tract->anatomy.jawFulcrum;
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

}  // namespace live
