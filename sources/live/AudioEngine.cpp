// ****************************************************************************
// Realtime audio engine for the ImGui live app.
// ****************************************************************************

#include "AudioEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "acoustics/TdsModel.h"
#include "anatomy/VocalTract.h"
#include "core/Constants.h"
#include "glottis/GeometricGlottis2019.h"
#include "glottis/GeometricGlottis2025.h"
#include "glottis/Glottis.h"
#include "glottis/TriangularGlottis.h"
#include "io/XmlNode.h"
#include "synthesis/Synthesizer.h"

#if !defined(__EMSCRIPTEN__)
#if defined(_WIN32)
#include <al.h>
#include <alc.h>
#elif defined(__APPLE__)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif
#endif  // !__EMSCRIPTEN__

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace live {

namespace {

#if !defined(__EMSCRIPTEN__)
// Synthesize 480 samples (= 10 ms @ 48 kHz) per OpenAL chunk. Big enough
// that the audio thread is not woken up too often, small enough that
// articulation changes feel responsive.
constexpr int CHUNK_SAMPLES = 480;
// 16 buffers × 480 samples / 48 kHz ≈ 160 ms of OpenAL queue headroom.
// Native builds run a dedicated audio thread so they never stall the queue;
// the extra slots cost only ~30 KB of int16 ringbuffer.
constexpr int NUM_AL_BUFFERS = 16;
#endif

// Number of glottis models the speaker file describes; matches the upstream
// API. We construct all three so that whichever the speaker selects can be
// activated without re-parsing.
enum {
  GEOMETRIC_GLOTTIS_2025 = 0,
  GEOMETRIC_GLOTTIS_2019 = 1,
  TRIANGULAR_GLOTTIS = 2,
  NUM_GLOTTIS_MODELS = 3,
};

bool loadSpeaker(const std::string& path, VocalTract* vocalTract,
                 Glottis* glottises[NUM_GLOTTIS_MODELS], int& selectedGlottis) {
  std::vector<XmlError> errors;
  XmlNode* rootNode = xmlParseFile(path, "speaker", &errors);
  if (rootNode == nullptr) {
    xmlPrintErrors(errors);
    return false;
  }

  selectedGlottis = GEOMETRIC_GLOTTIS_2025;

  XmlNode* glottisModelsNode = rootNode->getChildElement("glottis_models");
  if (glottisModelsNode != nullptr) {
    for (int i = 0; i < (int)glottisModelsNode->childElement.size() &&
                    i < NUM_GLOTTIS_MODELS;
         ++i) {
      XmlNode* glottisNode = glottisModelsNode->childElement[i];
      if (glottisNode->getAttributeString("type") == glottises[i]->getName()) {
        if (glottisNode->getAttributeInt("selected") == 1) selectedGlottis = i;
        glottises[i]->readFromXml(*glottisNode);
      } else {
        std::fprintf(stderr,
                     "live: unexpected glottis '%s' at slot %d (expected '%s')\n",
                     glottisNode->getAttributeString("type").c_str(), i,
                     glottises[i]->getName().c_str());
      }
    }
  }
  delete rootNode;

  try {
    vocalTract->readFromXml(path);
    vocalTract->calculateAll();
  } catch (const std::string& msg) {
    std::fprintf(stderr, "live: %s\nlive: failed to read anatomy from %s\n",
                 msg.c_str(), path.c_str());
    return false;
  }
  return true;
}

}  // namespace

// ----------------------------------------------------------------------------
// AudioHistory
// ----------------------------------------------------------------------------

void AudioHistory::push(const double* chunk, int n) {
  uint64_t w = writeIndex.load(std::memory_order_relaxed);
  for (int i = 0; i < n; ++i) {
    samples[(w + i) & (AUDIO_HISTORY_SIZE - 1)] = static_cast<float>(chunk[i]);
  }
  writeIndex.store(w + n, std::memory_order_release);
}

void AudioHistory::push(const float* chunk, int n) {
  uint64_t w = writeIndex.load(std::memory_order_relaxed);
  for (int i = 0; i < n; ++i) {
    samples[(w + i) & (AUDIO_HISTORY_SIZE - 1)] = chunk[i];
  }
  writeIndex.store(w + n, std::memory_order_release);
}

int AudioHistory::copyLatest(float* out, int n) const {
  uint64_t w = writeIndex.load(std::memory_order_acquire);
  if (n > AUDIO_HISTORY_SIZE) n = AUDIO_HISTORY_SIZE;
  uint64_t start = (w >= (uint64_t)n) ? (w - (uint64_t)n) : 0;
  int produced = static_cast<int>(w - start);
  for (int i = 0; i < produced; ++i) {
    out[i] = samples[(start + i) & (AUDIO_HISTORY_SIZE - 1)];
  }
  for (int i = produced; i < n; ++i) out[i] = 0.0f;
  return produced;
}

// ----------------------------------------------------------------------------
// AudioEngine
// ----------------------------------------------------------------------------

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() { stop(); }

const std::vector<VocalTract::Shape>& AudioEngine::tractShapes() const {
  return uiVocalTract->shapes;
}

const std::vector<Glottis::Shape>& AudioEngine::glottisShapes() const {
  return glottis->shapes;
}

int AudioEngine::numGlottisParams() const {
  return (int)glottis->controlParams.size();
}

const Glottis::Parameter& AudioEngine::glottisParamInfo(int i) const {
  return glottis->controlParams[i];
}

const VocalTract::Param& AudioEngine::tractParamInfo(int i) const {
  return uiVocalTract->params[i];
}

bool AudioEngine::start(const std::string& speakerFile) {
  if (running.load()) return true;

  // Audio-thread models.
  vocalTract = new VocalTract();
  vocalTract->calculateAll();

  Glottis* glottises[NUM_GLOTTIS_MODELS] = {
      new GeometricGlottis2025(),
      new GeometricGlottis2019(),
      new TriangularGlottis(),
  };

  int selectedGlottis = GEOMETRIC_GLOTTIS_2025;
  if (!loadSpeaker(speakerFile, vocalTract, glottises, selectedGlottis)) {
    std::fprintf(stderr, "live: failed to load speaker file %s\n",
                 speakerFile.c_str());
    delete vocalTract;
    vocalTract = nullptr;
    for (auto* g : glottises) delete g;
    return false;
  }
  glottis = glottises[selectedGlottis];
  for (int i = 0; i < NUM_GLOTTIS_MODELS; ++i) {
    if (i != selectedGlottis) delete glottises[i];
  }

  tdsModel = new TdsModel();
  synthesizer = new Synthesizer();
  synthesizer->init(glottis, vocalTract, tdsModel);

  // Preallocate the per-chunk scratch to the largest size either backend
  // will ever ask for: native uses 480-sample OpenAL chunks, the WASM
  // worklet uses up to WORKLET_MAX_QUANTUM. Capacity is sticky across
  // resize() so addChunk never has to allocate on the realtime path.
  renderScratch.assign(2048, 0.0);
  renderScratch.clear();

  // Discard any audio left over from a previous start (e.g. after a
  // speaker swap via restart()) so the new voice doesn't bleed into
  // a stale tail of samples from the old one.
  synthRingHead = 0;
  synthRingFill = 0;

  // UI-thread visualization tract: load the same anatomy + shapes so the
  // user can pick presets from the same list shown by the audio side.
  uiVocalTract = new VocalTract();
  uiVocalTract->calculateAll();
  try {
    uiVocalTract->readFromXml(speakerFile);
  } catch (const std::string& msg) {
    std::fprintf(stderr, "live: visualization tract load failed: %s\n",
                 msg.c_str());
  }

  // Seed control state from the loaded models. Default to the "a" shape if
  // it exists, otherwise leave the neutral params.
  {
    ControlState::Writer w(control);
    int n = numGlottisParams();
    control.glottisParamCount = n;
    for (int i = 0; i < n; ++i) {
      control.glottisParams[i] = glottis->controlParams[i].neutral;
    }
    control.f0_Hz = glottis->controlParams[Glottis::FREQUENCY].x > 0
                        ? glottis->controlParams[Glottis::FREQUENCY].x
                        : 120.0;
    control.pressure_dPa = 8000.0;

    int aIndex = uiVocalTract->getShapeIndex("a");
    if (aIndex >= 0) {
      const auto& shape = uiVocalTract->shapes[aIndex];
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        control.tractParams[i] = shape.param[i];
      }
    } else {
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        control.tractParams[i] = uiVocalTract->params[i].neutral;
      }
    }
    for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
      uiVocalTract->params[i].x = control.tractParams[i];
    }
    uiVocalTract->calculateAll();
  }

  // Prime the synthesizer with one zero-length chunk: Synthesizer::addChunk
  // captures the initial tube/glottis shapes on the first call and emits no
  // audio. Doing it here means the audio backend can assume the synthesizer
  // is ready, regardless of whether a thread is driving it (native) or the
  // worklet callback is (web).
  {
    std::vector<double> chunk;
    ControlSnapshot snap = control.snapshot();
    snap.glottisParams[Glottis::FREQUENCY] = snap.f0_Hz;
    snap.glottisParams[Glottis::PRESSURE] = snap.pressure_dPa;
    synthesizer->addChunk(snap.glottisParams, snap.tractParams, 0, chunk);
  }

#if !defined(__EMSCRIPTEN__)
  // ---- Native: open OpenAL device and start the audio thread. -------------
  ALCdevice* dev = alcOpenDevice(nullptr);
  if (dev == nullptr) {
    std::fprintf(stderr, "live: alcOpenDevice failed\n");
    stop();
    return false;
  }
  alDevice = dev;
  ALCcontext* ctx = alcCreateContext(dev, nullptr);
  if (ctx == nullptr || !alcMakeContextCurrent(ctx)) {
    std::fprintf(stderr, "live: alcCreateContext failed\n");
    if (ctx) alcDestroyContext(ctx);
    stop();
    return false;
  }
  alContext = ctx;

  ALuint src = 0;
  alGenSources(1, &src);
  alSource = src;
  alSourcef(src, AL_GAIN, control.outputGain);

  alBuffers.resize(NUM_AL_BUFFERS);
  alGenBuffers(NUM_AL_BUFFERS, alBuffers.data());

  running.store(true);
  speakerPath = speakerFile;
  thread = std::thread(&AudioEngine::threadMain, this);
#else
  // ---- WASM: leave AudioContext setup until the first user gesture. ------
  // The synthesizer is wired up; the AudioWorklet callback will start
  // pulling samples once requestAudioStart() (called from a click handler)
  // has resumed the context. Until then, running stays true so the worklet
  // outputs real audio the moment the context resumes.
  running.store(true);
  speakerPath = speakerFile;
#endif
  return true;
}

bool AudioEngine::restart(const std::string& speakerFile) {
  stop();
  return start(speakerFile);
}

void AudioEngine::stop() {
  // Disarm the audio side first so any concurrent reader (native audio
  // thread or WASM worklet callback) starts emitting silence and stops
  // touching the synth/glottis pointers we are about to delete.
  if (running.exchange(false)) {
#if !defined(__EMSCRIPTEN__)
    if (thread.joinable()) thread.join();
#else
    // The worklet runs on a browser-managed audio thread we cannot join.
    // Spin on the in-flight counter so an in-progress renderInto finishes
    // before we delete the synth pointers it touched. Bounded by a 100 ms
    // deadline so a hung worklet (e.g. tab suspended at the wrong moment)
    // doesn't lock the UI; in normal operation the wait resolves in well
    // under one quantum (~2.6 ms at 48 kHz / 128) since the callback is
    // already racing to produce its samples.
    if (workletNodeReady.load()) {
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(100);
      while (workletInflight.load(std::memory_order_acquire) > 0 &&
             std::chrono::steady_clock::now() < deadline) {
      }
    }
#endif
  }

#if !defined(__EMSCRIPTEN__)
  if (alSource) {
    alSourceStop(alSource);
    alDeleteSources(1, &alSource);
    alSource = 0;
  }
  if (!alBuffers.empty()) {
    alDeleteBuffers((ALsizei)alBuffers.size(), alBuffers.data());
    alBuffers.clear();
  }
  if (alContext) {
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(static_cast<ALCcontext*>(alContext));
    alContext = nullptr;
  }
  if (alDevice) {
    alcCloseDevice(static_cast<ALCdevice*>(alDevice));
    alDevice = nullptr;
  }
#endif

  delete synthesizer;
  synthesizer = nullptr;
  delete tdsModel;
  tdsModel = nullptr;
  delete glottis;
  glottis = nullptr;
  delete vocalTract;
  vocalTract = nullptr;
  delete uiVocalTract;
  uiVocalTract = nullptr;
}

void AudioEngine::refillSynthRing() {
  ControlSnapshot snap = control.snapshot();
  snap.glottisParams[Glottis::FREQUENCY] = snap.f0_Hz;
  snap.glottisParams[Glottis::PRESSURE] = snap.pressure_dPa;

  // renderScratch was sized in start() so addChunk's internal resize
  // stays within capacity and never reallocates from the realtime path.
  synthesizer->addChunk(snap.glottisParams, snap.tractParams,
                        SYNTH_CHUNK_SAMPLES, renderScratch);
  if ((int)renderScratch.size() < SYNTH_CHUNK_SAMPLES) {
    // addChunk emits an empty vector on the very first (priming) call;
    // fall back to silence so the ring stays in a valid state.
    renderScratch.assign(SYNTH_CHUNK_SAMPLES, 0.0);
  }

  history.push(renderScratch.data(), SYNTH_CHUNK_SAMPLES);

  const float gain = snap.outputGain;
  // Append into the ring contiguously. Capacity is 2 × chunk so we
  // always have room for a fresh chunk when the ring goes empty.
  int writePos = (synthRingHead + synthRingFill) % SYNTH_RING_CAP;
  for (int i = 0; i < SYNTH_CHUNK_SAMPLES; ++i) {
    double s = renderScratch[i] * gain;
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    synthRing[writePos] = static_cast<float>(s);
    writePos = (writePos + 1) % SYNTH_RING_CAP;
  }
  synthRingFill += SYNTH_CHUNK_SAMPLES;
}

void AudioEngine::renderInto(float* out, int numSamples) {
  if (numSamples <= 0) return;
  if (!running.load(std::memory_order_acquire) || synthesizer == nullptr) {
    for (int i = 0; i < numSamples; ++i) out[i] = 0.0f;
    return;
  }

  for (int i = 0; i < numSamples; ++i) {
    if (synthRingFill == 0) refillSynthRing();
    out[i] = synthRing[synthRingHead];
    synthRingHead = (synthRingHead + 1) % SYNTH_RING_CAP;
    --synthRingFill;
  }
}

#if !defined(__EMSCRIPTEN__)

// Internal state for produceOneChunkOpenAL(). Lives outside the class so we
// don't pollute the header with OpenAL types.
namespace {
struct ProduceState {
  ALuint nextBuffer = 0;
  bool playing = false;
  float lastGain = -1.0f;
};
ProduceState g_produceState;
}  // namespace

bool AudioEngine::produceOneChunkOpenAL() {
  // Quick check for a free OpenAL buffer slot. Returns false (without
  // synthesizing) when the queue is full and the player has not yet
  // consumed anything — the caller decides whether to sleep, yield, or
  // bail until next tick.
  ALint queued = 0, processed = 0;
  alGetSourcei(alSource, AL_BUFFERS_QUEUED, &queued);
  alGetSourcei(alSource, AL_BUFFERS_PROCESSED, &processed);
  ALuint freedBuf = 0;
  if (queued >= NUM_AL_BUFFERS) {
    if (processed <= 0) return false;
    alSourceUnqueueBuffers(alSource, 1, &freedBuf);
  }

  static thread_local std::vector<float> floatChunk(CHUNK_SAMPLES);
  static thread_local std::vector<int16_t> i16(CHUNK_SAMPLES);

  // renderInto already applies output gain and clamps to [-1, 1]; mute
  // the AL_GAIN path so we don't double-scale.
  if (g_produceState.lastGain != 1.0f) {
    alSourcef(alSource, AL_GAIN, 1.0f);
    g_produceState.lastGain = 1.0f;
  }
  renderInto(floatChunk.data(), CHUNK_SAMPLES);
  for (int i = 0; i < CHUNK_SAMPLES; ++i) {
    i16[i] = (int16_t)(floatChunk[i] * 32767.0f);
  }

  ALuint buf = alBuffers[g_produceState.nextBuffer];
  g_produceState.nextBuffer =
      (g_produceState.nextBuffer + 1) % NUM_AL_BUFFERS;
  alBufferData(buf, AL_FORMAT_MONO16, i16.data(),
               (ALsizei)(CHUNK_SAMPLES * sizeof(int16_t)),
               AUDIO_SAMPLING_RATE_HZ);
  alSourceQueueBuffers(alSource, 1, &buf);

  ALint state = 0;
  alGetSourcei(alSource, AL_SOURCE_STATE, &state);
  if (state != AL_PLAYING) alSourcePlay(alSource);
  g_produceState.playing = true;
  return true;
}

void AudioEngine::threadMain() {
  while (running.load()) {
    if (!produceOneChunkOpenAL()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  alSourceStop(alSource);
}

#else  // __EMSCRIPTEN__

// ---- WASM: AudioWorklet callback path -------------------------------------
//
// All of these functions are entered from emscripten's audio worklet
// scaffolding; the engine pointer travels through the userData arg.

bool AudioEngine::onAudioProcess(int /*numInputs*/, const AudioSampleFrame* /*inputs*/,
                                 int numOutputs, AudioSampleFrame* outputs,
                                 int /*numParams*/, const AudioParamFrame* /*params*/,
                                 void* userData) {
  AudioEngine* self = static_cast<AudioEngine*>(userData);
  // Bump the in-flight counter unconditionally on entry. stop() spins on
  // this before freeing synthesizer state, so as long as we decrement
  // before returning the synth pointer we just dereferenced is guaranteed
  // alive for the whole callback.
  self->workletInflight.fetch_add(1, std::memory_order_acq_rel);

  if (numOutputs <= 0 || outputs == nullptr || outputs[0].data == nullptr) {
    self->workletInflight.fetch_sub(1, std::memory_order_acq_rel);
    return true;
  }
  AudioSampleFrame& out = outputs[0];
  const int frames = out.samplesPerChannel;

  if (frames <= 0 || frames > WORKLET_MAX_QUANTUM) {
    // Defensive: fill silence and bail rather than overrun the stack-sized
    // scratch buffer. 128 is the spec quantum; 256 leaves headroom for
    // future Web Audio 1.1 renderSizeHints.
    for (int ch = 0; ch < out.numberOfChannels; ++ch) {
      float* dst = out.data + ch * out.samplesPerChannel;
      for (int i = 0; i < out.samplesPerChannel; ++i) dst[i] = 0.0f;
    }
    self->workletInflight.fetch_sub(1, std::memory_order_acq_rel);
    return true;
  }

  float scratch[WORKLET_MAX_QUANTUM];
  self->renderInto(scratch, frames);
  // Replicate the mono synth output into every channel the destination
  // asks for (typically just channel 0; Web Audio handles up-mixing).
  for (int ch = 0; ch < out.numberOfChannels; ++ch) {
    float* dst = out.data + ch * out.samplesPerChannel;
    for (int i = 0; i < frames; ++i) dst[i] = scratch[i];
  }
  self->workletInflight.fetch_sub(1, std::memory_order_acq_rel);
  return true;
}

void AudioEngine::onWorkletProcessorCreated(EMSCRIPTEN_WEBAUDIO_T ctx,
                                            bool success, void* userData) {
  if (!success) {
    std::fprintf(stderr, "live: audio worklet processor creation failed\n");
    return;
  }
  AudioEngine* self = static_cast<AudioEngine*>(userData);
  int outputChannelCounts[1] = {1};
  EmscriptenAudioWorkletNodeCreateOptions options{};
  options.numberOfInputs = 0;
  options.numberOfOutputs = 1;
  options.outputChannelCounts = outputChannelCounts;
  self->workletNode = emscripten_create_wasm_audio_worklet_node(
      ctx, "vtl-synth", &options, &AudioEngine::onAudioProcess, self);
  emscripten_audio_node_connect(self->workletNode, ctx, 0, 0);
  self->workletNodeReady.store(true, std::memory_order_release);
}

void AudioEngine::onWorkletThreadInited(EMSCRIPTEN_WEBAUDIO_T ctx, bool success,
                                        void* userData) {
  if (!success) {
    std::fprintf(stderr, "live: audio worklet thread init failed\n");
    return;
  }
  WebAudioWorkletProcessorCreateOptions opts{};
  opts.name = "vtl-synth";
  emscripten_create_wasm_audio_worklet_processor_async(
      ctx, &opts, &AudioEngine::onWorkletProcessorCreated, userData);
}

void AudioEngine::onContextResumed(EMSCRIPTEN_WEBAUDIO_T /*ctx*/,
                                   AUDIO_CONTEXT_STATE state, void* userData) {
  AudioEngine* self = static_cast<AudioEngine*>(userData);
  self->contextResumed.store(state == AUDIO_CONTEXT_STATE_RUNNING,
                             std::memory_order_release);
}

void AudioEngine::requestAudioStart() {
  // First call: build the AudioContext, kick off worklet thread setup, and
  // resume the context inside the user-gesture window so the browser
  // autoplay policy lets us start producing sound. The processor + node
  // come up asynchronously — once they connect, the worklet starts
  // pulling samples; the context resumed during this gesture is what
  // makes the browser deliver them to the speakers.
  if (!contextRequested.exchange(true)) {
    EmscriptenWebAudioCreateAttributes attrs{};
    attrs.latencyHint = "interactive";
    attrs.sampleRate = AUDIO_SAMPLING_RATE_HZ;
    attrs.renderSizeHint = AUDIO_CONTEXT_RENDER_SIZE_DEFAULT;
    audioContext = emscripten_create_audio_context(&attrs);
    emscripten_start_wasm_audio_worklet_thread_async(
        audioContext, workletStack, sizeof(workletStack),
        &AudioEngine::onWorkletThreadInited, this);
    emscripten_resume_audio_context_async(audioContext,
                                          &AudioEngine::onContextResumed, this);
    return;
  }
  // Subsequent calls (e.g. tab unbackgrounded): nudge the context back to
  // running. Cheap and idempotent if already playing.
  if (audioContext && !contextResumed.load(std::memory_order_acquire)) {
    emscripten_resume_audio_context_async(audioContext,
                                          &AudioEngine::onContextResumed, this);
  }
}

bool AudioEngine::audioContextRunning() const {
  return contextResumed.load(std::memory_order_acquire) &&
         workletNodeReady.load(std::memory_order_acquire);
}

void AudioEngine::threadMain() {
  // No background thread on WASM; the worklet drives the synth via the
  // audio worklet callback.
}

#endif  // __EMSCRIPTEN__

}  // namespace live
