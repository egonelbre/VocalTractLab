// Standalone WASM benchmark for the vtl synthesis pipeline.
//
// Mirrors synthesis_bench.cpp, but uses a tiny custom harness instead of
// Google Benchmark (which is not readily available under the Emscripten
// toolchain we use). Designed to be run with Node:
//
//   node build/wasm-bench/wasm_bench.js [min_seconds]
//
// For each scenario the harness re-runs the workload until either the
// per-iteration count target is reached or `min_seconds` of wall time has
// passed, then reports the *median* per-iteration time. Median is more
// robust than the mean against the JIT-warmup spike v8 pays on the first
// iteration of any new code path.
//
// Data files are surfaced through Emscripten's MEMFS via --preload-file
// (matching the convention used by vtl_live), so the API can open them by
// the same well-known path on every backend.
//
// All timing uses emscripten_get_now(), which under the AudioWorklet thread
// is performance.now() and is the same clock the live build uses for its
// real-time deadline tracking — so RTF numbers reported here are directly
// comparable to what the worklet sees in production.

#include <emscripten/emscripten.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "VocalTractLabApi.h"

namespace {

// Match vtl_live's preload layout. CMake's --preload-file maps source paths
// to these virtual locations.
constexpr const char* kSpeakerFile = "/JD2.speaker";
constexpr const char* kGesturalScoreFile = "/example01.ges";
constexpr int kAudioSampleRate = 48000;
constexpr int kBlockSamples = 120;  // 2.5 ms

double now_ms() { return emscripten_get_now(); }

struct Result {
  std::string name;
  double median_us = 0.0;
  double min_us = 0.0;
  double mean_us = 0.0;
  int iterations = 0;
  double rtf = 0.0;            // audio s produced per wall s (when applicable)
  double block_audio_ms = 0.0; // audio ms per iteration (when applicable)
};

// Run `body` until either iter_target is hit or min_seconds elapse.
// Returns wall-time samples per iteration, in microseconds.
Result run_bench(const std::string& name, double min_seconds, int iter_target,
                 std::function<void(int)> body, int audio_samples_per_iter) {
  // Warmup: 3 untimed iterations to let v8 tier up.
  for (int i = 0; i < 3; ++i) body(0);

  std::vector<double> samples;
  samples.reserve(static_cast<size_t>(iter_target));
  double start = now_ms();
  int it = 0;
  while (it < iter_target) {
    double t0 = now_ms();
    body(it);
    double t1 = now_ms();
    samples.push_back((t1 - t0) * 1000.0);  // ms → µs
    ++it;
    if ((now_ms() - start) >= min_seconds * 1000.0 && it >= 5) break;
  }

  std::sort(samples.begin(), samples.end());
  Result r;
  r.name = name;
  r.iterations = static_cast<int>(samples.size());
  r.median_us = samples[samples.size() / 2];
  r.min_us = samples.front();
  double sum = 0.0;
  for (double s : samples) sum += s;
  r.mean_us = sum / samples.size();
  if (audio_samples_per_iter > 0) {
    const double audio_us = (static_cast<double>(audio_samples_per_iter) /
                             kAudioSampleRate) * 1.0e6;
    r.block_audio_ms = audio_us / 1000.0;
    r.rtf = audio_us / r.median_us;
  }
  return r;
}

void report(const Result& r) {
  std::printf("%-32s median=%9.1f us  min=%9.1f us  mean=%9.1f us  iters=%4d",
              r.name.c_str(), r.median_us, r.min_us, r.mean_us, r.iterations);
  if (r.block_audio_ms > 0.0) {
    std::printf("  block_audio=%.2f ms  RTF=%.3f", r.block_audio_ms, r.rtf);
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char** argv) {
  double min_seconds = 1.0;
  if (argc >= 2) min_seconds = std::atof(argv[1]);

  std::printf("vtl wasm_bench: min_seconds=%.2f, clock=emscripten_get_now()\n",
              min_seconds);

  if (vtlInitialize(kSpeakerFile) != 0) {
    std::fprintf(stderr, "vtlInitialize failed (speaker=%s)\n", kSpeakerFile);
    return 1;
  }

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  // ---- BM_TractToTube --------------------------------------------------
  {
    std::vector<double> tractParams(numTract);
    if (vtlGetTractParams("a", tractParams.data()) != 0) {
      std::fprintf(stderr, "vtlGetTractParams(a) failed\n");
      return 1;
    }
    std::vector<double> tubeLength(numTube), tubeArea(numTube);
    std::vector<int> tubeArtic(numTube);
    double incisorPos = 0, velumOpening = 0, tongueTipSide = 0;
    auto body = [&](int) {
      vtlTractToTube(tractParams.data(), tubeLength.data(), tubeArea.data(),
                     tubeArtic.data(), &incisorPos, &tongueTipSide,
                     &velumOpening);
    };
    auto r = run_bench("BM_TractToTube", min_seconds, 600, body, 0);
    report(r);
  }

  // ---- BM_GetTransferFunction -----------------------------------------
  {
    std::vector<double> tractParams(numTract);
    vtlGetTractParams("a", tractParams.data());
    const int numSpec = 2048;
    std::vector<double> magnitude(numSpec), phase(numSpec);
    auto body = [&](int) {
      vtlGetTransferFunction(tractParams.data(), numSpec, magnitude.data(),
                             phase.data());
    };
    auto r = run_bench("BM_GetTransferFunction", min_seconds, 600, body, 0);
    report(r);
  }

  // ---- BM_SynthesisAddTube_Block ---------------------------------------
  {
    std::vector<double> tractParams(numTract);
    if (vtlGetTractParams("a", tractParams.data()) != 0) {
      std::fprintf(stderr, "vtlGetTractParams(a) failed\n");
      return 1;
    }
    std::vector<double> tubeLength(numTube), tubeArea(numTube);
    std::vector<int> tubeArtic(numTube);
    double incisorPos = 0, velumOpening = 0, tongueTipSide = 0;
    vtlTractToTube(tractParams.data(), tubeLength.data(), tubeArea.data(),
                   tubeArtic.data(), &incisorPos, &tongueTipSide,
                   &velumOpening);

    std::vector<char> names(10 * numGlottis);
    std::vector<double> gMin(numGlottis), gMax(numGlottis), gNeutral(numGlottis);
    vtlGetGlottisParamInfo(names.data(), gMin.data(), gMax.data(),
                           gNeutral.data());

    char glottisName[] = "Geometric glottis 2025";
    if (vtlResetTubeSynthesis(glottisName, gNeutral.data(), 17.0, 11.0, 0.0,
                              0.0) != 0) {
      std::fprintf(stderr, "vtlResetTubeSynthesis failed\n");
      return 1;
    }

    std::vector<double> block(kBlockSamples);
    // First call after reset: 0 samples to seed.
    vtlSynthesisAddTube(0, block.data(), tubeLength.data(), tubeArea.data(),
                        tubeArtic.data(), incisorPos, velumOpening,
                        tongueTipSide, gNeutral.data());

    auto body = [&](int) {
      vtlSynthesisAddTube(kBlockSamples, block.data(), tubeLength.data(),
                          tubeArea.data(), tubeArtic.data(), incisorPos,
                          velumOpening, tongueTipSide, gNeutral.data());
    };
    auto r = run_bench("BM_SynthesisAddTube_Block", min_seconds, 4000, body,
                       kBlockSamples);
    report(r);
  }

  // ---- BM_SynthesisAddTract_Held (held vowel via tract API) -----------
  {
    std::vector<double> tractParams(numTract);
    if (vtlGetTractParams("a", tractParams.data()) != 0) {
      std::fprintf(stderr, "vtlGetTractParams(a) failed\n");
      return 1;
    }
    std::vector<char> names(10 * numGlottis);
    std::vector<double> gMin(numGlottis), gMax(numGlottis), gNeutral(numGlottis);
    vtlGetGlottisParamInfo(names.data(), gMin.data(), gMax.data(),
                           gNeutral.data());

    if (vtlResetTractSynthesis() != 0) {
      std::fprintf(stderr, "vtlResetTractSynthesis failed\n");
      return 1;
    }
    std::vector<double> block(kBlockSamples);
    vtlSynthesisAddTract(0, block.data(), tractParams.data(), gNeutral.data());

    auto body = [&](int) {
      vtlSynthesisAddTract(kBlockSamples, block.data(), tractParams.data(),
                           gNeutral.data());
    };
    auto r = run_bench("BM_SynthesisAddTract_Held", min_seconds, 2000, body,
                       kBlockSamples);
    report(r);
  }

  // ---- BM_GesturalScoreToAudio (end-to-end) ----------------------------
  {
    std::vector<double> audio(kAudioSampleRate * 30);
    int numSamples = static_cast<int>(audio.size());
    auto body = [&](int) {
      numSamples = static_cast<int>(audio.size());
      int rc = vtlGesturalScoreToAudio(kGesturalScoreFile, "", "", audio.data(),
                                       &numSamples, /*normalizeAudio=*/0,
                                       /*enableConsoleOutput=*/0);
      if (rc != 0) {
        std::fprintf(stderr, "vtlGesturalScoreToAudio failed (rc=%d)\n", rc);
        std::exit(1);
      }
    };
    // End-to-end takes >1 s per iter; cap to a few iterations.
    auto r = run_bench("BM_GesturalScoreToAudio", min_seconds, 5, body,
                       numSamples > 0 ? numSamples : 1);
    // The audio_samples_per_iter we passed only matters for RTF; recompute
    // from the *final* numSamples so the reported audio_s is accurate.
    if (numSamples > 0) {
      const double audio_us =
          (static_cast<double>(numSamples) / kAudioSampleRate) * 1.0e6;
      r.block_audio_ms = audio_us / 1000.0;
      r.rtf = audio_us / r.median_us;
    }
    report(r);
  }

  vtlClose();
  return 0;
}
