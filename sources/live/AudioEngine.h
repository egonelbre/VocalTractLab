// ****************************************************************************
// Realtime audio + control state for the ImGui live app.
// ****************************************************************************

#ifndef LIVE_AUDIO_ENGINE_H_
#define LIVE_AUDIO_ENGINE_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "anatomy/VocalTract.h"
#include "glottis/Glottis.h"

class TdsModel;
class Synthesizer;

namespace live {

// Number of audio samples kept around for the spectrum view.
// Power of two so we can mask instead of modulo. ~170 ms at 48 kHz.
constexpr int AUDIO_HISTORY_SIZE = 8192;

// Single-producer / multi-reader-snapshot history. The audio thread appends
// chunks and bumps writeIndex; the UI thread snapshots the most recent N
// samples to feed the spectrum FFT and the time-signal preview.
struct AudioHistory {
  std::atomic<uint64_t> writeIndex{0};
  float samples[AUDIO_HISTORY_SIZE]{};

  void push(const double* chunk, int n);
  // Copies the latest n samples (chronological order) into out. Returns
  // how many of those samples are real history (the rest are zero-padded
  // when the audio thread has not produced enough yet).
  int copyLatest(float* out, int n) const;
};

// Mutable state the UI writes and the audio thread reads each chunk.
struct ControlState {
  std::mutex mtx;

  // Primary controls.
  double f0_Hz = 120.0;
  double pressure_dPa = 8000.0;

  // Articulation parameters (one slider per VocalTract param, plus shape
  // presets).
  double tractParams[VocalTract::NUM_PARAMS] = {};

  // Glottis control parameters (length matches the selected glottis model).
  // Indices Glottis::FREQUENCY and Glottis::PRESSURE are overwritten from
  // f0_Hz / pressure_dPa right before each chunk is rendered, so the user
  // does not have to keep them in sync manually.
  std::vector<double> glottisParams;

  // OpenAL output gain (0..1). The audio thread keeps producing samples
  // even when this is 0 so the spectrum view stays live.
  float outputGain = 0.6f;
};

class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  // Loads the given speaker file, opens an OpenAL device, and spawns the
  // synthesis thread. Returns false if anything fails.
  bool start(const std::string& speakerFile);
  void stop();

  // Public state shared between threads.
  ControlState control;
  AudioHistory history;

  // Read-only access to data loaded from the speaker file. Safe to read from
  // the UI thread because the engine never mutates these after start().
  const std::vector<VocalTract::Shape>& tractShapes() const;
  const std::vector<Glottis::Shape>& glottisShapes() const;
  int numGlottisParams() const;
  const Glottis::Parameter& glottisParamInfo(int i) const;
  const VocalTract::Param& tractParamInfo(int i) const;

  // UI-side VocalTract used purely for drawing (lives on the UI thread).
  // The audio thread has its own VocalTract instance.
  VocalTract* uiTract() { return uiVocalTract; }

 private:
  void threadMain();

  std::atomic<bool> running{false};
  std::thread thread;

  // Models owned by the audio thread.
  Glottis* glottis = nullptr;
  VocalTract* vocalTract = nullptr;
  TdsModel* tdsModel = nullptr;
  Synthesizer* synthesizer = nullptr;

  // Models owned by the UI thread (drawing only).
  VocalTract* uiVocalTract = nullptr;

  // OpenAL state. Stored as void* / unsigned int so the public header
  // does not pull in <OpenAL/al.h>.
  void* alDevice = nullptr;
  void* alContext = nullptr;
  unsigned int alSource = 0;
  std::vector<unsigned int> alBuffers;
};

}  // namespace live

#endif  // LIVE_AUDIO_ENGINE_H_
