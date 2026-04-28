// ****************************************************************************
// Realtime audio + control state for the ImGui live app.
// ****************************************************************************

#ifndef LIVE_AUDIO_ENGINE_H_
#define LIVE_AUDIO_ENGINE_H_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "anatomy/VocalTract.h"
#include "glottis/Glottis.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/webaudio.h>
#endif

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
  void push(const float* chunk, int n);
  // Copies the latest n samples (chronological order) into out. Returns
  // how many of those samples are real history (the rest are zero-padded
  // when the audio thread has not produced enough yet).
  int copyLatest(float* out, int n) const;
};

// Plain snapshot of ControlState that the audio thread reads each chunk.
struct ControlSnapshot {
  double f0_Hz;
  double pressure_dPa;
  double tractParams[VocalTract::NUM_PARAMS];
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];
  int glottisParamCount;
  float outputGain;
  // When true, the synthesizer recomputes TRX/TRY from the tongue body
  // position on every chunk and ignores the slider values; matches the
  // speaker file's <root automatic_calc="1"/> behaviour. Mirrored onto
  // VocalTract::anatomy.automaticTongueRootCalc by the audio thread.
  bool autoTongueRoot;
};

// Mutable state shared between the UI thread (writer) and the audio thread
// (reader). Synchronised with a seqlock instead of std::mutex so the audio
// worklet thread on WASM can read consistently without pulling in pthread:
// the writer bumps `seq` to odd before mutating fields and back to even
// when done; the reader retries until two reads of `seq` agree and the
// value is even.
//
// The seqlock is correct for our single-writer (UI) / single-reader (audio)
// pattern. UI updates are infrequent (slider drags at frame rate) so the
// reader almost never retries.
struct ControlState {
  // Even = stable, odd = writer mid-update.
  std::atomic<uint32_t> seq{0};

  // Plain (non-atomic) state. Always read/written under the seqlock.
  double f0_Hz = 120.0;
  double pressure_dPa = 8000.0;
  double tractParams[VocalTract::NUM_PARAMS] = {};
  // Fixed-size glottis params buffer so the snapshot copy is a plain
  // memcpy and the buffer cannot be resized while the audio thread is
  // mid-read. The active length lives in glottisParamCount and is set
  // once per speaker by AudioEngine::start.
  double glottisParams[Glottis::MAX_CONTROL_PARAMS] = {};
  int glottisParamCount = 0;
  // Output gain (0..1) applied at the audio output. The audio thread keeps
  // producing samples even when this is 0 so the spectrum view stays live.
  float outputGain = 0.6f;
  // Mirrors VocalTract::anatomy.automaticTongueRootCalc. Off by default
  // for the live app so the Tongue root sliders take effect; UI can flip
  // it back on via the Articulation panel checkbox.
  bool autoTongueRoot = false;

  // RAII writer guard. Construct once per logical update; the destructor
  // closes the seqlock window so the audio thread can observe a consistent
  // snapshot. The release fence in the destructor ensures the field stores
  // are visible before the seq counter goes back to even.
  class Writer {
   public:
    explicit Writer(ControlState& s) : s_(s) {
      s_.seq.fetch_add(1, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_release);
    }
    ~Writer() {
      std::atomic_thread_fence(std::memory_order_release);
      s_.seq.fetch_add(1, std::memory_order_relaxed);
    }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

   private:
    ControlState& s_;
  };

  // Audio-thread snapshot. Spins while the writer holds the lock; in
  // practice resolves in a single iteration because writer windows are a
  // few field assignments long. The acquire fence sandwiched between the
  // field reads and the second seq read prevents the field reads from
  // being reordered past the validation.
  ControlSnapshot snapshot() const {
    ControlSnapshot snap{};
    for (;;) {
      uint32_t s1 = seq.load(std::memory_order_acquire);
      if (s1 & 1u) continue;  // mid-write, retry
      snap.f0_Hz = f0_Hz;
      snap.pressure_dPa = pressure_dPa;
      snap.outputGain = outputGain;
      snap.autoTongueRoot = autoTongueRoot;
      snap.glottisParamCount = glottisParamCount;
      std::memcpy(snap.tractParams, tractParams, sizeof(tractParams));
      std::memcpy(snap.glottisParams, glottisParams, sizeof(glottisParams));
      std::atomic_thread_fence(std::memory_order_acquire);
      uint32_t s2 = seq.load(std::memory_order_relaxed);
      if (s1 == s2) return snap;
    }
  }
};

class AudioEngine {
 public:
  AudioEngine();
  ~AudioEngine();

  // Loads the given speaker file, opens the audio backend, and starts
  // synthesis. On native this spawns a dedicated audio thread and opens
  // an OpenAL device. On WASM this initialises the synthesizer and arms
  // the AudioWorklet thread; the worklet processor + node are created
  // asynchronously by ensureAudioContext (called once a user gesture is
  // available so the browser will let us resume the AudioContext).
  // Returns false if anything fails.
  bool start(const std::string& speakerFile);
  void stop();

  // Tear the running engine down (stop()) and immediately bring it back
  // up against a different speaker file. Lets the UI swap speakers at
  // runtime without losing the audio device. Returns false if start()
  // fails on the new speaker — the engine is left stopped in that case.
  bool restart(const std::string& speakerFile);

  // Path passed to the most recent successful start(); empty before
  // first start. Used by the UI to know which speaker is currently
  // loaded so the switcher can highlight the right segment.
  const std::string& currentSpeakerPath() const { return speakerPath; }

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

#if defined(__EMSCRIPTEN__)
  // Web Audio requires a user gesture before audio output can resume.
  // The UI calls this from any handler that fires inside a click /
  // pointerdown / keydown event; first call kicks off the asynchronous
  // worklet thread + processor + node setup, subsequent calls just
  // resume the AudioContext if the browser has suspended it. Safe to
  // call every frame — internal flags make repeat calls cheap no-ops.
  void requestAudioStart();
  // True once the worklet node is connected and the AudioContext has
  // resumed. The UI uses this to hide its "Click to start audio" hint.
  bool audioContextRunning() const;
#endif

 private:
  void threadMain();
#if !defined(__EMSCRIPTEN__)
  // Native OpenAL pump: returns true if it was able to (un)queue a buffer
  // slot and produce one chunk; false if the OpenAL queue is full and no
  // buffer has been processed yet.
  bool produceOneChunkOpenAL();
#endif

  // Renders `numSamples` of audio into `out` (float, mono, [-1,1]). Pulls
  // samples from an internal ring that is refilled in fixed-size synth
  // chunks (so vocalTract->calculateAll() runs at the synth's natural
  // chunk rate even when the worklet asks for 128-sample quanta). Called
  // from the native audio thread (wrapped by produceOneChunkOpenAL) and
  // from the WASM AudioWorklet callback. Single-thread access only.
  void renderInto(float* out, int numSamples);

  // Synthesise SYNTH_CHUNK_SAMPLES worth of audio into the internal ring,
  // applying output gain and clamping. Called by renderInto whenever the
  // ring is empty.
  void refillSynthRing();

  std::atomic<bool> running{false};
  std::thread thread;
  std::string speakerPath;

  // Models owned by the audio thread.
  Glottis* glottis = nullptr;
  VocalTract* vocalTract = nullptr;
  TdsModel* tdsModel = nullptr;
  Synthesizer* synthesizer = nullptr;

  // Scratch buffer that refillSynthRing passes to Synthesizer::addChunk.
  // Owned by the engine (not thread_local) so allocation happens once at
  // start() — thread_local on a Web AudioWorklet thread has historically
  // been brittle, and pre-allocation also keeps the realtime callback off
  // malloc.
  std::vector<double> renderScratch;

  // Ring buffer of post-gain audio sitting between the synth (which we
  // run in fixed-size chunks for steady CPU load) and the consumer (the
  // OpenAL queue on native, the AudioWorklet quantum on WASM). Both ends
  // run on the same thread, so a plain head/tail pair is enough.
  //
  // Sized to match the Web Audio worklet quantum (128 samples / 2.67 ms
  // @ 48 kHz). On the WASM build with E1's tract cache landed,
  // refillSynthRing now finishes in well under one quantum even on a
  // tablet, so producing one chunk per worklet callback gives a flat
  // CPU-load curve — every quantum does the same amount of work, no
  // ~7-quanta sawtooth where one callback ran 480 samples' worth of
  // synth in a 128-sample budget. Native (OpenAL queue, dedicated audio
  // thread) is unaffected: it asks renderInto for whatever size it
  // wants and the loop just iterates the refill.
  static constexpr int SYNTH_CHUNK_SAMPLES = 128;  // 1 worklet quantum @ 48 kHz
  static constexpr int SYNTH_RING_CAP = SYNTH_CHUNK_SAMPLES * 2;
  float synthRing[SYNTH_RING_CAP] = {};
  int synthRingHead = 0;  // next sample to read
  int synthRingFill = 0;  // samples currently buffered

  // Models owned by the UI thread (drawing only).
  VocalTract* uiVocalTract = nullptr;

#if !defined(__EMSCRIPTEN__)
  // OpenAL state. Stored as void* / unsigned int so the public header
  // does not pull in <OpenAL/al.h>.
  void* alDevice = nullptr;
  void* alContext = nullptr;
  unsigned int alSource = 0;
  std::vector<unsigned int> alBuffers;
#else
  // Emscripten Web Audio + AudioWorklet state.
  // The audio context, worklet thread, processor, and node are created
  // once and reused across speaker swaps so we never have to wait on the
  // browser's async setup mid-session. Synthesizer state is rebuilt on
  // start()/stop()/restart() while the worklet keeps running and emits
  // silence whenever `running` is false.
  EMSCRIPTEN_WEBAUDIO_T audioContext = 0;
  EMSCRIPTEN_AUDIO_WORKLET_NODE_T workletNode = 0;
  std::atomic<bool> contextRequested{false};   // set after first user gesture
  std::atomic<bool> workletNodeReady{false};   // set when processor/node up
  std::atomic<bool> contextResumed{false};     // set after resume callback
  // Tracks whether the worklet callback is currently inside renderInto. The
  // worklet bumps it on entry and clears it on exit; stop() spins on it
  // before deleting synth pointers so an in-flight callback never touches
  // freed memory.
  std::atomic<int> workletInflight{0};
  // Per-quantum render buffer reused across worklet callbacks. Capacity is
  // the worklet quantum size (128 samples in the Web Audio 1.0 spec); we
  // pre-allocate one chunk so the realtime callback never touches malloc.
  static constexpr int WORKLET_MAX_QUANTUM = 256;
  // The audio worklet thread needs a C stack for the callback. The synth
  // call chain (Synthesizer::addChunk -> VocalTract::calculateAll ->
  // calcSurfaces -> ...) bottoms out around 120 KB of stack per chunk —
  // calcSurfaces alone, with its mesh-builder helpers and Point3D temporaries
  // splattered across the call frames, eats most of that. 256 KB leaves
  // ~100 KB of headroom; an AudioEngine instance is heap/BSS-resident, so
  // the extra memory cost is negligible.
  alignas(16) uint8_t workletStack[262144]{};

  // Static thunks the emscripten C API hands the worklet runtime.
  static void onWorkletThreadInited(EMSCRIPTEN_WEBAUDIO_T ctx, bool success,
                                    void* userData);
  static void onWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T ctx, bool success,
                                        void* userData);
  static bool onAudioProcess(int numInputs, const AudioSampleFrame* inputs,
                             int numOutputs, AudioSampleFrame* outputs,
                             int numParams, const AudioParamFrame* params,
                             void* userData);
  static void onContextResumed(EMSCRIPTEN_WEBAUDIO_T ctx, AUDIO_CONTEXT_STATE st,
                               void* userData);
#endif
};

}  // namespace live

#endif  // LIVE_AUDIO_ENGINE_H_
