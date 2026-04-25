#ifndef LIVE_VOCAL_TRACT_3D_PANEL_H_
#define LIVE_VOCAL_TRACT_3D_PANEL_H_

class VocalTract;

namespace live {

// Renders the "Vocal Tract 3D" ImGui window: software-projected wireframe
// of the same set of surfaces VocalTractPicture::renderWireFrame paints in
// the wxGLCanvas, with mouse-orbit camera and depth-fade for back-facing
// lines. Reads tract->surfaces directly; the caller is responsible for
// having called calculateAll() with the desired articulation already.
void renderVocalTract3DPanel(VocalTract* tract);

}  // namespace live

#endif  // LIVE_VOCAL_TRACT_3D_PANEL_H_
