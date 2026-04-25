#ifndef LIVE_CONTROLS_PANEL_H_
#define LIVE_CONTROLS_PANEL_H_

#include <vector>

namespace live {

class AudioEngine;

// Per-frame copy of the engine's mutable control state. The UI works
// against this copy (so it never holds the engine's mutex while running
// ImGui), and writeFrameSnapshot() commits the result back at the end of
// the frame. Indices Glottis::FREQUENCY / Glottis::PRESSURE in the
// glottis vector are kept in sync with f0_Hz / pressure_dPa.
struct FrameSnapshot {
  double f0_Hz = 120.0;
  double pressure_dPa = 8000.0;
  float outputGain = 0.6f;
  std::vector<double> tractParams;     // length VocalTract::NUM_PARAMS
  std::vector<double> glottisParams;   // length engine.numGlottisParams()
};

FrameSnapshot readFrameSnapshot(AudioEngine& engine);
void writeFrameSnapshot(AudioEngine& engine, const FrameSnapshot& snap);

// Renders the "Controls" ImGui window. Mutates snap in place.
void renderControlsPanel(AudioEngine& engine, FrameSnapshot& snap);

}  // namespace live

#endif  // LIVE_CONTROLS_PANEL_H_
