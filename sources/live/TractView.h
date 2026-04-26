#ifndef LIVE_TRACT_VIEW_H_
#define LIVE_TRACT_VIEW_H_

#include "imgui.h"

class VocalTract;

namespace live {

// Affine transform between mediosagittal model coordinates (cm; +x right, +y
// up) and ImGui screen coordinates (+y down). Constructed from the canvas
// rectangle by computeTractView so the bounding box of the tract fits inside
// the canvas with the right aspect ratio.
struct TractView {
  ImVec2 origin;          // screen pixel that maps to (0, 0) in cm
  float scale_px_per_cm;  // uniform scale (preserves aspect ratio)

  ImVec2 toScreen(double x_cm, double y_cm) const {
    return ImVec2(origin.x + (float)x_cm * scale_px_per_cm,
                  origin.y - (float)y_cm * scale_px_per_cm);
  }
  ImVec2 toModel(ImVec2 screen) const {
    return ImVec2((screen.x - origin.x) / scale_px_per_cm,
                  (origin.y - screen.y) / scale_px_per_cm);
  }
};

// Axis-aligned bounding box of the tract's visible anatomy in cm,
// computed from the actual surface vertices so it tracks articulation
// changes (and is robust to per-speaker anatomy variation).
struct TractBounds {
  double minX, maxX, minY, maxY, minZ, maxZ;
  double cx() const { return (minX + maxX) * 0.5; }
  double cy() const { return (minY + maxY) * 0.5; }
  double cz() const { return (minZ + maxZ) * 0.5; }
  double width() const { return maxX - minX; }
  double height() const { return maxY - minY; }
  double depth() const { return maxZ - minZ; }
  double maxExtent() const {
    double w = width(), h = height(), d = depth();
    double m = w > h ? w : h;
    return d > m ? d : m;
  }
};

TractBounds computeVisibleBounds(VocalTract* tract);

// Bounding box that encloses the tract across the full slider range,
// so the 2D / 3D views can be sized once and stay stable while the
// user articulates. Lazily computed on the first call by sweeping
// each VocalTract parameter to its min and max (others held at their
// current value), unioning the per-pose visible bounds. Subsequent
// calls return the cached result; the calculation is ~40 calculateAll
// invocations and happens once for the lifetime of the process.
const TractBounds& articulationExtentBounds(VocalTract* tract);

// Fit the canvas to the tract's XY bounding box (with a small margin so
// dragged control points don't sit right against the edge).
TractView computeTractView(ImVec2 canvasMin, ImVec2 canvasMax,
                           const TractBounds& bounds);

}  // namespace live

#endif  // LIVE_TRACT_VIEW_H_
