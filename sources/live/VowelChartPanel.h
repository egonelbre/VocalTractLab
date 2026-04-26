#ifndef LIVE_VOWEL_CHART_PANEL_H_
#define LIVE_VOWEL_CHART_PANEL_H_

namespace live {

class AudioEngine;
struct FrameSnapshot;

// Renders the "Vowel Chart" ImGui window: an F1/F2 plot with the
// cardinal English/IPA vowels at their canonical formant positions.
// Clicking or dragging in the chart morphs snap.tractParams toward
// the surrounding vowel shapes via inverse-distance weighting on
// the speaker file's tract shape parameters; a live dot shows the
// current tract's actual F1/F2 (computed from the TL model).
void renderVowelChartPanel(AudioEngine& engine, FrameSnapshot& snap);

}  // namespace live

#endif  // LIVE_VOWEL_CHART_PANEL_H_
