#ifndef LIVE_VOCAL_TRACT_2D_PANEL_H_
#define LIVE_VOCAL_TRACT_2D_PANEL_H_

class VocalTract;

namespace live {

// Renders the "Vocal Tract" ImGui window: mediosagittal outline + draggable
// control points. tract->params is overwritten from tractParams (length must
// be VocalTract::NUM_PARAMS) and calculateAll() is invoked before drawing.
// User drags write back into tractParams in-place. When autoTongueRoot is
// true, the tongue back / tongue root handle is hidden and not pickable —
// the synthesizer derives TRX/TRY from the tongue body position so the
// handle can't be moved meaningfully.
void renderVocalTract2DPanel(VocalTract* tract, double* tractParams,
                             bool autoTongueRoot);

}  // namespace live

#endif  // LIVE_VOCAL_TRACT_2D_PANEL_H_
