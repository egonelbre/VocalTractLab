#include "TractView.h"

#include <algorithm>

namespace live {

TractView computeTractView(ImVec2 canvasMin, ImVec2 canvasMax) {
  // The bounding box of the JD2 vocal tract in mediosagittal coordinates is
  // roughly x ∈ [-12, 8] cm, y ∈ [-12, 4] cm — center around (-2, -4).
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

}  // namespace live
