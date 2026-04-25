#ifndef LIVE_VOCAL_TRACT_2D_PANEL_H_
#define LIVE_VOCAL_TRACT_2D_PANEL_H_

class VocalTract;

namespace live {

// Renders the "Vocal Tract" ImGui window: mediosagittal outline + draggable
// control points. tract->params is overwritten from tractParams (length must
// be VocalTract::NUM_PARAMS) and calculateAll() is invoked before drawing.
// User drags write back into tractParams in-place.
void renderVocalTract2DPanel(VocalTract* tract, double* tractParams);

}  // namespace live

#endif  // LIVE_VOCAL_TRACT_2D_PANEL_H_
