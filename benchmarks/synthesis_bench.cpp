// Synthesis benchmarks for the vtl backend.
//
// Reports a real-time factor (RTF) counter where applicable: RTF = audio
// duration produced per unit wall time. RTF = 1.0 means the synthesis just
// keeps up with playback; RTF < 1.0 means real-time playback would underrun.
//
// The benchmarks use the public C API (VocalTractLab API) and load the JD2
// speaker plus example01.ges from the in-tree data/ directory.

#include <benchmark/benchmark.h>

#include <cstring>
#include <vector>

#include "VocalTractLabApi.h"
#include "benchmark_data.h"

using namespace vtl_bench;

namespace {

// Initialize the API once for the whole process. The speaker file load is
// expensive (XML parse + geometry build) and the API is a global singleton,
// so paying that cost once amortizes it across all benchmarks.
struct ApiInitializer {
  ApiInitializer() { vtlInitialize(kSpeakerFile); }
  ~ApiInitializer() { vtlClose(); }
};
ApiInitializer& Api() {
  static ApiInitializer init;
  return init;
}

}  // namespace

// ---------------------------------------------------------------------------
// End-to-end: gestural score → audio.
//
// Loads example01.ges (~2.5 s of audio), runs it through the full pipeline
// (parse → tube sequence → TDS → audio). RTF reflects steady-state throughput
// of a typical interactive synthesis run.
static void BM_GesturalScoreToAudio(benchmark::State& state) {
  Api();

  // example01.ges produces ~2.5 s of audio; allocate generously.
  std::vector<double> audio(kAudioSampleRate * 30);
  int numSamples = 0;

  for (auto _ : state) {
    numSamples = static_cast<int>(audio.size());
    int rc = vtlGesturalScoreToAudio(kGesturalScoreFile, "", "", audio.data(),
                                     &numSamples,
                                     /*normalizeAudio=*/0,
                                     /*enableConsoleOutput=*/0);
    if (rc != 0) {
      state.SkipWithError("vtlGesturalScoreToAudio failed");
      return;
    }
    benchmark::DoNotOptimize(audio.data());
  }

  const double audio_seconds = static_cast<double>(numSamples) / kAudioSampleRate;
  state.counters["audio_s"] = audio_seconds;
  state.counters["RTF"] = benchmark::Counter(
      audio_seconds, benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_GesturalScoreToAudio)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// Per-block tube synthesis (steady state).
//
// vtlSynthesisAddTube is the natural real-time entry point: callers feed it
// 120-sample (2.5 ms) blocks for each new vocal tract state. A real-time
// pipeline must finish each call in < 2.5 ms wall time.
static void BM_SynthesisAddTube_Block(benchmark::State& state) {
  Api();

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  // Hold a single vowel ("a") tube shape for the duration of the benchmark.
  std::vector<double> tractParams(numTract);
  if (vtlGetTractParams("a", tractParams.data()) != 0) {
    state.SkipWithError("vtlGetTractParams(\"a\") failed");
    return;
  }

  std::vector<double> tubeLength(numTube), tubeArea(numTube);
  std::vector<int> tubeArtic(numTube);
  double incisorPos = 0, velumOpening = 0, tongueTipSide = 0;
  vtlTractToTube(tractParams.data(), tubeLength.data(), tubeArea.data(),
                 tubeArtic.data(), &incisorPos, &tongueTipSide, &velumOpening);

  // Neutral glottis params for the default model.
  std::vector<char> names(10 * numGlottis);
  std::vector<double> gMin(numGlottis), gMax(numGlottis), gNeutral(numGlottis);
  vtlGetGlottisParamInfo(names.data(), gMin.data(), gMax.data(), gNeutral.data());

  char glottisName[] = "Geometric glottis 2025";
  if (vtlResetTubeSynthesis(glottisName, gNeutral.data(),
                            /*tracheaLength_cm=*/17.0,
                            /*noseLength_cm=*/11.0,
                            /*piriformFossaLength_cm=*/0.0,
                            /*piriformFossaVolume_cm3=*/0.0) != 0) {
    state.SkipWithError("vtlResetTubeSynthesis failed");
    return;
  }

  std::vector<double> block(kBlockSamples);

  // First call after reset must use numNewSamples=0 to seed initial tube state.
  vtlSynthesisAddTube(0, block.data(), tubeLength.data(), tubeArea.data(),
                      tubeArtic.data(), incisorPos, velumOpening, tongueTipSide,
                      gNeutral.data());

  for (auto _ : state) {
    int rc = vtlSynthesisAddTube(kBlockSamples, block.data(), tubeLength.data(),
                                 tubeArea.data(), tubeArtic.data(), incisorPos,
                                 velumOpening, tongueTipSide, gNeutral.data());
    if (rc != 0) {
      state.SkipWithError("vtlSynthesisAddTube failed");
      return;
    }
    benchmark::DoNotOptimize(block.data());
  }

  const double audio_s_per_iter =
      static_cast<double>(kBlockSamples) / kAudioSampleRate;
  state.counters["block_audio_ms"] = audio_s_per_iter * 1000.0;
  state.counters["RTF"] = benchmark::Counter(
      audio_s_per_iter, benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_SynthesisAddTube_Block)->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Tract-to-tube geometry conversion only (no acoustics).
//
// A useful baseline: every vtlSynthesisAddTract call internally does this.
// Knowing the standalone cost tells us how much of the per-block budget the
// geometry layer consumes vs. the acoustic solver.
static void BM_TractToTube(benchmark::State& state) {
  Api();

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  std::vector<double> tractParams(numTract);
  vtlGetTractParams("a", tractParams.data());

  std::vector<double> tubeLength(numTube), tubeArea(numTube);
  std::vector<int> tubeArtic(numTube);
  double incisorPos = 0, velumOpening = 0, tongueTipSide = 0;

  for (auto _ : state) {
    vtlTractToTube(tractParams.data(), tubeLength.data(), tubeArea.data(),
                   tubeArtic.data(), &incisorPos, &tongueTipSide, &velumOpening);
    benchmark::DoNotOptimize(tubeLength.data());
    benchmark::DoNotOptimize(tubeArea.data());
  }
}
BENCHMARK(BM_TractToTube)->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// Frequency-domain transfer function (TlModel).
//
// Independent code path from TDS — useful as a comparison point for analysis
// workloads.
static void BM_GetTransferFunction(benchmark::State& state) {
  Api();

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  std::vector<double> tractParams(numTract);
  vtlGetTractParams("a", tractParams.data());

  const int numSpec = 2048;
  std::vector<double> magnitude(numSpec), phase(numSpec);

  for (auto _ : state) {
    vtlGetTransferFunction(tractParams.data(), numSpec, magnitude.data(),
                           phase.data());
    benchmark::DoNotOptimize(magnitude.data());
  }
}
BENCHMARK(BM_GetTransferFunction)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
