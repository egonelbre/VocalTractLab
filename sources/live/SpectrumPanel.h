#ifndef LIVE_SPECTRUM_PANEL_H_
#define LIVE_SPECTRUM_PANEL_H_

class ComplexSignal;
class VocalTract;

namespace live {

struct AudioHistory;

// Length of the FFT taken for the spectrum display. 1024 samples ≈ 21 ms at
// 48 kHz, narrow enough to be responsive yet wide enough to resolve the
// formants.
constexpr int FFT_LEN_EXPONENT = 10;
constexpr int FFT_LEN = 1 << FFT_LEN_EXPONENT;

// Frequency-axis mode for the primary spectrum. Open enum so future scales
// (e.g. Mel, Bark, ERB) can be added without breaking call sites.
enum class FreqScale {
  Linear,
  Log,
};

// Renders the "Primary Spectrum" ImGui window. Overlays four things:
//   1. FFT magnitude of the latest audio output (from history).
//   2. The vocal-tract transfer function computed from uiTract via TlModel.
//   3. A harmonic comb at n*f0_Hz so you can see which model peaks the
//      current fundamental actually excites.
//   4. Formant frequencies (F1..F8) extracted from the same TlModel.
// (An antiresonance overlay exists in the implementation but is gated
// off until the depth heuristic stops flickering on non-nasal vowels.)
// fft is reused as scratch space and must hold at least FFT_LEN samples.
// f0_Hz is taken by reference so a click/drag on the piano keyboard
// strip at the bottom of the panel can snap the fundamental to the
// nearest MIDI note in place.
void renderSpectrumPanel(AudioHistory& history, ComplexSignal& fft,
                         VocalTract* uiTract, double& f0_Hz);

}  // namespace live

#endif  // LIVE_SPECTRUM_PANEL_H_
