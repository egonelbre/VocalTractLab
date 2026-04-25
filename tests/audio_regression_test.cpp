// Audio regression tests for the vtl synthesis pipeline.
//
// Each subtest exercises one of the code paths the optimization work touched
// (geometry, transfer function, end-to-end synthesis) and hashes the raw
// output bytes with FNV-1a. The synthesizer is deterministic — TdsModel
// reseeds its RNG to a constant — so the hashes are stable across runs and
// any change in output indicates a real behavior drift, not noise.
//
// On hash mismatch the test prints expected/actual and, for the audio test,
// dumps the actual samples as `audio_regression_actual.raw` next to the cwd
// so the failure can be inspected (load as float64 mono @ 48 kHz).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "VocalTractLabApi.h"
#include "test_data.h"

namespace {

// FNV-1a 64-bit. Hashes the raw byte representation of a buffer; tiny
// floating-point drifts cascade through the hash, which is what we want
// for catching unintended numerical changes.
uint64_t fnv1a(const void *data, size_t len) {
  const uint8_t *p = static_cast<const uint8_t*>(data);
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

template <typename T>
uint64_t hashSpan(const T *data, size_t count) {
  return fnv1a(data, count * sizeof(T));
}

bool checkHash(const char *label, uint64_t actual, uint64_t expected) {
  // Pass `expected = 0` to just print the actual value (capture mode for
  // generating the baseline). Real hashes are never 0 in practice.
  if (expected == 0) {
    std::printf("  CAPTURE  %-27s 0x%016llxULL\n",
                label, (unsigned long long)actual);
    return true;
  }
  if (actual == expected) {
    std::printf("  PASS     %-27s 0x%016llx\n",
                label, (unsigned long long)actual);
    return true;
  }
  std::printf("  FAIL     %-27s\n", label);
  std::printf("           expected 0x%016llx\n", (unsigned long long)expected);
  std::printf("           got      0x%016llx\n", (unsigned long long)actual);
  return false;
}

bool checkInt(const char *label, long actual, long expected) {
  if (expected == 0) {
    std::printf("  CAPTURE  %-27s %ld\n", label, actual);
    return true;
  }
  if (actual == expected) {
    std::printf("  PASS     %-27s %ld\n", label, actual);
    return true;
  }
  std::printf("  FAIL     %-27s expected %ld, got %ld\n",
              label, expected, actual);
  return false;
}

// ---------------------------------------------------------------------------
// Geometry: vtlTractToTube on the "a" vowel shape.
// Touches Surface intersection / cross-section profile code — the heaviest
// area of recent optimization (dedup, merge, classify cache, etc).

bool testTractToTube() {
  std::printf("[TractToTube]\n");

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  std::vector<double> tractParams(numTract);
  if (vtlGetTractParams("a", tractParams.data()) != 0) {
    std::printf("  vtlGetTractParams failed\n");
    return false;
  }

  std::vector<double> tubeLength(numTube), tubeArea(numTube);
  std::vector<int> tubeArtic(numTube);
  double incisorPos = 0, velumOpening = 0, tongueTipSide = 0;

  vtlTractToTube(tractParams.data(), tubeLength.data(), tubeArea.data(),
                 tubeArtic.data(), &incisorPos, &tongueTipSide, &velumOpening);

  bool ok = true;
  ok &= checkHash("tubeLength",
                  hashSpan(tubeLength.data(), tubeLength.size()),
                  0x9c3eb3f7bd934e29ULL);
  ok &= checkHash("tubeArea",
                  hashSpan(tubeArea.data(), tubeArea.size()),
                  0x28aeb9b8e380bcf6ULL);
  ok &= checkHash("tubeArtic",
                  hashSpan(tubeArtic.data(), tubeArtic.size()),
                  0xd289c032fb79e2f0ULL);

  const double scalars[3] = {incisorPos, velumOpening, tongueTipSide};
  ok &= checkHash("scalars(incisor/velum/tip)",
                  fnv1a(scalars, sizeof(scalars)),
                  0xf11c011759609b07ULL);
  return ok;
}

// ---------------------------------------------------------------------------
// Transfer function: vtlGetTransferFunction on the "a" vowel shape.
// Independent code path (TlModel, frequency domain).

bool testTransferFunction() {
  std::printf("[TransferFunction]\n");

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  std::vector<double> tractParams(numTract);
  if (vtlGetTractParams("a", tractParams.data()) != 0) {
    std::printf("  vtlGetTractParams failed\n");
    return false;
  }

  const int numSpec = 2048;
  std::vector<double> magnitude(numSpec), phase(numSpec);
  vtlGetTransferFunction(tractParams.data(), numSpec,
                         magnitude.data(), phase.data());

  bool ok = true;
  ok &= checkHash("magnitude",
                  hashSpan(magnitude.data(), magnitude.size()),
                  0x34c9924abb72529aULL);
  ok &= checkHash("phase",
                  hashSpan(phase.data(), phase.size()),
                  0xa0c1779ba78c03c3ULL);
  return ok;
}

// ---------------------------------------------------------------------------
// End-to-end: gestural score → audio.
// Touches everything: parser, geometry, TDS solver, post-processing.

bool testGesturalScoreToAudio() {
  std::printf("[GesturalScoreToAudio]\n");

  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  // example01.ges produces ~2.5 s of audio; allocate generously.
  std::vector<double> audio(static_cast<size_t>(audioSr) * 30);
  int numSamples = static_cast<int>(audio.size());

  int rc = vtlGesturalScoreToAudio(vtl_test::kGesturalScoreFile, "", "",
                                   audio.data(), &numSamples,
                                   /*normalizeAudio=*/0,
                                   /*enableConsoleOutput=*/0);
  if (rc != 0) {
    std::printf("  vtlGesturalScoreToAudio rc=%d\n", rc);
    return false;
  }

  bool ok = true;
  ok &= checkInt("numSamples", numSamples, 108720);
  const uint64_t audioHash =
      hashSpan(audio.data(), static_cast<size_t>(numSamples));
  if (!checkHash("audio samples", audioHash, 0xaf07347ccb6921f5ULL)) {
    // Drop the actual samples next to the test binary so the audible
    // difference can be inspected.
    if (FILE *f = std::fopen("audio_regression_actual.raw", "wb")) {
      std::fwrite(audio.data(), sizeof(double),
                  static_cast<size_t>(numSamples), f);
      std::fclose(f);
      std::printf("  wrote audio_regression_actual.raw "
                  "(%d samples, float64 mono @ %d Hz)\n",
                  numSamples, audioSr);
    }
    ok = false;
  }
  return ok;
}

}  // namespace

int main() {
  if (vtlInitialize(vtl_test::kSpeakerFile) != 0) {
    std::fprintf(stderr, "vtlInitialize failed for %s\n",
                 vtl_test::kSpeakerFile);
    return 2;
  }

  bool ok = true;
  ok &= testTractToTube();
  ok &= testTransferFunction();
  ok &= testGesturalScoreToAudio();

  vtlClose();

  std::printf("\n%s\n", ok ? "OK" : "FAIL");
  return ok ? 0 : 1;
}
