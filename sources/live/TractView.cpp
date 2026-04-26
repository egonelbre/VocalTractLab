#include "TractView.h"

#include <algorithm>
#include <limits>

#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Geometry.h"

namespace live {

TractBounds computeVisibleBounds(VocalTract* tract) {
  const double inf = std::numeric_limits<double>::infinity();
  TractBounds b{ inf, -inf, inf, -inf, inf, -inf };
  // Surfaces that contribute to the visible 2D / 3D anatomy. Iterating
  // a fixed list (rather than every Surface in the tract) keeps the
  // bbox tight: it ignores the inner-cover and rib-construction
  // surfaces that sit deep inside and would otherwise inflate the box.
  static const int visibleSurfaces[] = {
      VocalTract::UPPER_COVER,  VocalTract::LOWER_COVER,
      VocalTract::UVULA,        VocalTract::EPIGLOTTIS,
      VocalTract::TONGUE,
      VocalTract::UPPER_TEETH,  VocalTract::LOWER_TEETH,
      VocalTract::UPPER_LIP,    VocalTract::LOWER_LIP,
  };
  for (int idx : visibleSurfaces) {
    Surface* s = &tract->surfaces[idx];
    for (int i = 0; i < s->numRibs; ++i) {
      for (int j = 0; j < s->numRibPoints; ++j) {
        Point3D q = s->getVertex(i, j);
        if (q.x < b.minX) b.minX = q.x;
        if (q.x > b.maxX) b.maxX = q.x;
        if (q.y < b.minY) b.minY = q.y;
        if (q.y > b.maxY) b.maxY = q.y;
        if (q.z < b.minZ) b.minZ = q.z;
        if (q.z > b.maxZ) b.maxZ = q.z;
      }
    }
  }
  // Defensive fallback if the tract somehow has no vertices: pick a
  // plausible 1 cm box so downstream divisions don't blow up.
  if (b.minX > b.maxX) {
    b = TractBounds{ -0.5, 0.5, -0.5, 0.5, -0.5, 0.5 };
  }
  return b;
}

namespace {

void unionInto(TractBounds& dst, const TractBounds& src) {
  if (src.minX < dst.minX) dst.minX = src.minX;
  if (src.maxX > dst.maxX) dst.maxX = src.maxX;
  if (src.minY < dst.minY) dst.minY = src.minY;
  if (src.maxY > dst.maxY) dst.maxY = src.maxY;
  if (src.minZ < dst.minZ) dst.minZ = src.minZ;
  if (src.maxZ > dst.maxZ) dst.maxZ = src.maxZ;
}

}  // namespace

const TractBounds& articulationExtentBounds(VocalTract* tract) {
  // Cache keyed on the tract pointer so a future hot-reload of a
  // different speaker file invalidates automatically. Today the live
  // app only loads one tract for the lifetime of the process.
  static TractBounds cached{};
  static VocalTract* cachedFor = nullptr;
  if (cachedFor == tract) return cached;

  // Snapshot the current articulation so we can restore it after the
  // sweep — callers expect the tract geometry to be unchanged.
  double saved[VocalTract::NUM_PARAMS];
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    saved[i] = tract->params[i].x;
  }

  TractBounds m = computeVisibleBounds(tract);
  // Sweep each parameter independently to its slider extremes with
  // the others held at the current articulation. Joint extremes can
  // theoretically push slightly further but in practice the
  // axis-wise union covers the visible envelope.
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    double original = saved[i];
    double mn = tract->params[i].min;
    double mx = tract->params[i].max;
    tract->params[i].x = mn;
    tract->calculateAll();
    unionInto(m, computeVisibleBounds(tract));
    tract->params[i].x = mx;
    tract->calculateAll();
    unionInto(m, computeVisibleBounds(tract));
    tract->params[i].x = original;
  }

  // Restore.
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    tract->params[i].x = saved[i];
  }
  tract->calculateAll();

  cached = m;
  cachedFor = tract;
  return cached;
}

TractView computeTractView(ImVec2 canvasMin, ImVec2 canvasMax,
                           const TractBounds& bounds) {
  // 1 cm margin on every side. Keeps the lip / uvula control handles
  // off the canvas edge and gives drag-out room.
  constexpr double margin_cm = 1.0;
  double modelW = bounds.width() + 2.0 * margin_cm;
  double modelH = bounds.height() + 2.0 * margin_cm;
  if (modelW < 1.0) modelW = 1.0;
  if (modelH < 1.0) modelH = 1.0;
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  float scale = std::min(canvasW / (float)modelW, canvasH / (float)modelH);
  TractView view;
  view.scale_px_per_cm = scale;
  view.origin = ImVec2(canvasMin.x + canvasW * 0.5f -
                           (float)bounds.cx() * scale,
                       canvasMin.y + canvasH * 0.5f +
                           (float)bounds.cy() * scale);
  return view;
}

}  // namespace live
