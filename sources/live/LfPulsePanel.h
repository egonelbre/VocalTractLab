#ifndef LIVE_LF_PULSE_PANEL_H_
#define LIVE_LF_PULSE_PANEL_H_

namespace live {

// Renders the "Glottal Pulse" ImGui window. Shows one period of the LF
// (Liljencrants–Fant) glottal flow model alongside its time derivative —
// the latter is the actual excitation source seen by the vocal tract.
//
// f0_Hz is used only to label the time axis with the resulting period
// length. The panel exposes the LF shape parameters (OQ, SQ, TL) as
// reference sliders; they do not feed synthesis (the live build drives a
// geometric glottis model), they only reshape this preview.
void renderLfPulsePanel(double f0_Hz);

}  // namespace live

#endif  // LIVE_LF_PULSE_PANEL_H_
