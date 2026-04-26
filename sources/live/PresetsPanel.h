#ifndef LIVE_PRESETS_PANEL_H_
#define LIVE_PRESETS_PANEL_H_

#include <string>
#include <vector>

struct ImFont;

namespace live {

class AudioEngine;
struct FrameSnapshot;

// One entry in the speaker switcher at the top of the panel.
//   displayName  short label used on the segmented button
//   path         full path passed to AudioEngine::restart, either an
//                absolute filesystem path (native) or a leading-slash
//                emscripten virtual-FS path (wasm preload-file mount)
struct SpeakerOption {
  std::string displayName;
  std::string path;
};

// Renders the "Tract Shapes" panel:
//   * a segmented speaker switcher across the top (one button per
//     entry in `speakers`; the active speaker is highlighted),
//   * every preset shape from the currently loaded speaker as a
//     small button, grouped by phonetic category so individual
//     buttons can stay short (e.g. "a", "i", "u") instead of
//     carrying long composite names like "tt-alveolar-fricative(a)".
//
// Switching speakers calls AudioEngine::restart and refreshes the
// frame snapshot from the new engine state in place.
//
// `buttonFont` is an optional ImGui font to push only around the
// shape buttons — typically a re-rasterized variant of the UI font
// at a larger pixel size so the IPA labels read crisply. Pass null
// to render at the default font.
void renderTractShapesPanel(AudioEngine& engine, FrameSnapshot& snap,
                            const std::vector<SpeakerOption>& speakers,
                            ImFont* buttonFont = nullptr);

}  // namespace live

#endif  // LIVE_PRESETS_PANEL_H_
