#ifndef LIVE_CONTROL_POINTS_H_
#define LIVE_CONTROL_POINTS_H_

#include "imgui.h"

class VocalTract;

namespace live {

// Interactive control points the user can drag on the 2D mediosagittal view.
// Mirrors the wxGLCanvas-based VocalTractPicture::setControlPoint /
// parameterToControlPoint pair. Each handle reads/writes a small set of
// vocal tract parameters and runs them through restrictParam /
// restrictTongueParams to keep them inside the model's allowed range.
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
  ImVec2 modelPos;  // (cm, cm) in the mediosagittal plane
};

const char* controlPointLabel(int kind);

// Reads the model state and returns where the handle should sit in model
// coordinates. tract->calculateAll() must have been run on the current
// parameters before this is called.
ControlPointInfo getControlPoint(VocalTract* tract, int kind);

// Updates the parameter array (and the matching tract->params[].x slots)
// such that the named control point lands at modelTarget. Internally calls
// restrictParam / restrictTongueParams; the caller is responsible for
// re-running calculateAll() afterwards.
void applyControlPointDrag(VocalTract* tract, double* params, int kind,
                           ImVec2 modelTarget);

}  // namespace live

#endif  // LIVE_CONTROL_POINTS_H_
