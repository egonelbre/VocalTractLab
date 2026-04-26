#ifndef LIVE_SPECTROGRAM_PANEL_H_
#define LIVE_SPECTROGRAM_PANEL_H_

namespace live {

struct AudioHistory;

// Renders the "Spectrogram" ImGui window: a scrolling time-frequency view
// of the most recent audio history, with horizontal markers at n*f0_Hz so
// you can see the harmonic stack walking up/down with the fundamental.
//
// Recomputes the STFT every frame from history (the buffer is small enough
// that this is much cheaper than maintaining a scrolling cell grid).
void renderSpectrogramPanel(AudioHistory& history, double f0_Hz);

}  // namespace live

#endif  // LIVE_SPECTROGRAM_PANEL_H_
