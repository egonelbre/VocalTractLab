#ifndef LIVE_SPECTRUM_PANEL_H_
#define LIVE_SPECTRUM_PANEL_H_

class ComplexSignal;

namespace live {

struct AudioHistory;

// Length of the FFT taken for the spectrum display. 1024 samples ≈ 21 ms at
// 48 kHz, narrow enough to be responsive yet wide enough to resolve the
// formants.
constexpr int FFT_LEN_EXPONENT = 10;
constexpr int FFT_LEN = 1 << FFT_LEN_EXPONENT;

// Renders the "Primary Spectrum" ImGui window. Reads the latest FFT_LEN
// samples from history and reuses fft as scratch space (it must be large
// enough; FFT_LEN is fine).
void renderSpectrumPanel(AudioHistory& history, ComplexSignal& fft);

}  // namespace live

#endif  // LIVE_SPECTRUM_PANEL_H_
