#ifndef LIVE_TRACT_VIEW_H_
#define LIVE_TRACT_VIEW_H_

#include "imgui.h"

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

TractView computeTractView(ImVec2 canvasMin, ImVec2 canvasMax);

}  // namespace live

#endif  // LIVE_TRACT_VIEW_H_
