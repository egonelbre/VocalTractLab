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

namespace live {

namespace {

// Synthesize 480 samples (= 10 ms @ 48 kHz) per chunk. Big enough that the
// audio thread is not woken up too often, small enough that articulation
// changes feel responsive. We keep four buffers in flight, so the worst-case
// latency is ~40 ms.
constexpr int CHUNK_SAMPLES = 480;
constexpr int NUM_AL_BUFFERS = 4;

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
    std::lock_guard<std::mutex> lk(control.mtx);
    int n = numGlottisParams();
    control.glottisParams.assign(n, 0.0);
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

  // OpenAL setup.
  ALCdevice* dev = alcOpenDevice(nullptr);
  if (dev == nullptr) {
    std::fprintf(stderr, "live: alcOpenDevice failed\n");
    return false;
  }
  alDevice = dev;
  ALCcontext* ctx = alcCreateContext(dev, nullptr);
  if (ctx == nullptr || !alcMakeContextCurrent(ctx)) {
    std::fprintf(stderr, "live: alcCreateContext failed\n");
    if (ctx) alcDestroyContext(ctx);
    alcCloseDevice(dev);
    alDevice = nullptr;
    return false;
  }
  alContext = ctx;

  ALuint src = 0;
  alGenSources(1, &src);
  alSource = src;
  alSourcef(src, AL_GAIN, control.outputGain);

  alBuffers.resize(NUM_AL_BUFFERS);
  alGenBuffers(NUM_AL_BUFFERS, alBuffers.data());

  // Prime the synthesizer with one zero-length chunk: Synthesizer::addChunk
  // captures the initial tube/glottis shapes on the first call and emits no
  // audio. Doing it here means produceOneChunk() can assume the synthesizer
  // is ready, regardless of whether a thread is driving it (native) or the
  // main loop is (web).
  {
    std::vector<double> chunk;
    std::vector<double> tractParamsLocal(VocalTract::NUM_PARAMS);
    std::vector<double> glottisParamsLocal(numGlottisParams());
    std::lock_guard<std::mutex> lk(control.mtx);
    for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
      tractParamsLocal[i] = control.tractParams[i];
    }
    for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
      glottisParamsLocal[i] = control.glottisParams[i];
    }
    glottisParamsLocal[Glottis::FREQUENCY] = control.f0_Hz;
    glottisParamsLocal[Glottis::PRESSURE] = control.pressure_dPa;
    synthesizer->addChunk(glottisParamsLocal.data(), tractParamsLocal.data(),
                          0, chunk);
  }

  running.store(true);
#if !defined(__EMSCRIPTEN__)
  // Native: a dedicated audio thread paces synthesis to realtime, freeing
  // the UI from doing it. On web (Emscripten) the host can't easily spawn
  // threads — we let the main loop call pumpMainThread() instead.
  thread = std::thread(&AudioEngine::threadMain, this);
#endif
  return true;
}

void AudioEngine::stop() {
  if (running.exchange(false)) {
    if (thread.joinable()) thread.join();
  }

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

// Internal state for produceOneChunk(). Lives inside AudioEngine so both the
// thread and the main-loop pump share buffer cursors and gain caching.
namespace {
struct ProduceState {
  ALuint nextBuffer = 0;
  bool playing = false;
  float lastGain = -1.0f;
};
ProduceState g_produceState;
}  // namespace

bool AudioEngine::produceOneChunk() {
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

  thread_local std::vector<double> chunk;
  thread_local std::vector<int16_t> i16(CHUNK_SAMPLES);
  thread_local std::vector<double> tractParamsLocal(VocalTract::NUM_PARAMS);
  thread_local std::vector<double> glottisParamsLocal;
  if ((int)glottisParamsLocal.size() != numGlottisParams()) {
    glottisParamsLocal.assign(numGlottisParams(), 0.0);
  }
  if ((int)i16.size() != CHUNK_SAMPLES) i16.resize(CHUNK_SAMPLES);

  float gain;
  {
    std::lock_guard<std::mutex> lk(control.mtx);
    for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
      tractParamsLocal[i] = control.tractParams[i];
    }
    for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
      glottisParamsLocal[i] = control.glottisParams[i];
    }
    glottisParamsLocal[Glottis::FREQUENCY] = control.f0_Hz;
    glottisParamsLocal[Glottis::PRESSURE] = control.pressure_dPa;
    gain = control.outputGain;
  }

  if (gain != g_produceState.lastGain) {
    alSourcef(alSource, AL_GAIN, gain);
    g_produceState.lastGain = gain;
  }

  synthesizer->addChunk(glottisParamsLocal.data(), tractParamsLocal.data(),
                        CHUNK_SAMPLES, chunk);
  if ((int)chunk.size() != CHUNK_SAMPLES) chunk.resize(CHUNK_SAMPLES, 0.0);
  history.push(chunk.data(), CHUNK_SAMPLES);

  for (int i = 0; i < CHUNK_SAMPLES; ++i) {
    double s = chunk[i];
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    i16[i] = (int16_t)(s * 32767.0);
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
    if (!produceOneChunk()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  alSourceStop(alSource);
}

void AudioEngine::pumpMainThread(int maxChunks) {
  if (!running.load()) return;
  for (int i = 0; i < maxChunks; ++i) {
    if (!produceOneChunk()) return;
  }
}

}  // namespace live
