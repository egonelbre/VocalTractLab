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

#include <cmath>
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

bool testTractToTubeOne(const char *vowel,
                        uint64_t expectLen, uint64_t expectArea,
                        uint64_t expectArtic, uint64_t expectScalars) {
  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);

  std::vector<double> tractParams(numTract);
  if (vtlGetTractParams(vowel, tractParams.data()) != 0) {
    std::printf("  vtlGetTractParams(\"%s\") failed\n", vowel);
    return false;
  }

  std::vector<double> tubeLength(numTube), tubeArea(numTube);
  std::vector<int> tubeArtic(numTube);
  double incisorPos = 0, velumOpening = 0, tongueTipSide = 0;

  vtlTractToTube(tractParams.data(), tubeLength.data(), tubeArea.data(),
                 tubeArtic.data(), &incisorPos, &tongueTipSide, &velumOpening);

  char buf[64];
  bool ok = true;
  std::snprintf(buf, sizeof(buf), "tubeLength[%s]", vowel);
  ok &= checkHash(buf, hashSpan(tubeLength.data(), tubeLength.size()), expectLen);
  std::snprintf(buf, sizeof(buf), "tubeArea[%s]", vowel);
  ok &= checkHash(buf, hashSpan(tubeArea.data(), tubeArea.size()), expectArea);
  std::snprintf(buf, sizeof(buf), "tubeArtic[%s]", vowel);
  ok &= checkHash(buf, hashSpan(tubeArtic.data(), tubeArtic.size()), expectArtic);

  const double scalars[3] = {incisorPos, velumOpening, tongueTipSide};
  std::snprintf(buf, sizeof(buf), "scalars[%s]", vowel);
  ok &= checkHash(buf, fnv1a(scalars, sizeof(scalars)), expectScalars);
  return ok;
}

bool testTractToTube() {
  std::printf("[TractToTube]\n");
  bool ok = true;
  // Hashes captured from a pre-optimization build at d6227f4 (parent of
  // the geometry-perf series). Each row exercises a different vocal-tract
  // shape; "i" / "u" / "E" / "@" pull cuts through more cross-section
  // profile slopes than "a" does, which is what initially exposed an
  // FP-order bug in setupProfileLine.
  ok &= testTractToTubeOne("a",
      0x9c3eb3f7bd934e29ULL, 0x28aeb9b8e380bcf6ULL,
      0xd289c032fb79e2f0ULL, 0xf11c011759609b07ULL);
  ok &= testTractToTubeOne("i",
      0x4fa15abc12cd70c5ULL, 0x408ea310c5165badULL,
      0x58005d23a7ceec46ULL, 0xfcdf7c20a3f90908ULL);
  ok &= testTractToTubeOne("u",
      0x89e5469a144604adULL, 0x06a24a49042dc33dULL,
      0xde7333aa62175bd3ULL, 0x60bbc5c00be86fa9ULL);
  ok &= testTractToTubeOne("E",
      0x719317183e820f1dULL, 0xdc98e9b5573c6cf2ULL,
      0xb281d3ae1e6e58e3ULL, 0xee2c84b2a112c6faULL);
  ok &= testTractToTubeOne("@",
      0xb168814ba8d115ddULL, 0x21b7ebce9293390cULL,
      0x45208a2b9fc703b5ULL, 0x2bf70ed00ae0b424ULL);
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
  if (!checkHash("audio samples", audioHash, 0xcb0962f03ef5b19aULL)) {
    // If a reference dump from a known-good build is sitting next to us,
    // quantify the drift (max-abs diff, RMS, peak audio level) so it's
    // easy to tell "tiny FP noise" from "completely broken".
    if (FILE *r = std::fopen("audio_regression_reference.raw", "rb")) {
      std::vector<double> ref(numSamples);
      const size_t n = std::fread(ref.data(), sizeof(double),
                                  static_cast<size_t>(numSamples), r);
      std::fclose(r);
      if (n == static_cast<size_t>(numSamples)) {
        double maxAbs = 0.0, sumSq = 0.0, refPeak = 0.0;
        for (int i = 0; i < numSamples; i++) {
          const double d = audio[i] - ref[i];
          const double ad = d < 0.0 ? -d : d;
          if (ad > maxAbs) maxAbs = ad;
          sumSq += d * d;
          const double a = audio[i] < 0.0 ? -audio[i] : audio[i];
          if (a > refPeak) refPeak = a;
        }
        const double rms = std::sqrt(sumSq / numSamples);
        std::printf("  drift vs audio_regression_reference.raw: "
                    "max|d|=%.3e rms=%.3e (peak=%.3e)\n",
                    maxAbs, rms, refPeak);
      }
    }

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

// One-shot helper: synthesize the gestural score and write the audio to
// audio_regression_reference.raw so a subsequent run can quantify drift.
static int dumpReference() {
  int audioSr = 0, numTube = 0, numTract = 0, numGlottis = 0;
  vtlGetConstants(&audioSr, &numTube, &numTract, &numGlottis);
  std::vector<double> audio(static_cast<size_t>(audioSr) * 30);
  int numSamples = static_cast<int>(audio.size());
  if (vtlGesturalScoreToAudio(vtl_test::kGesturalScoreFile, "", "",
                              audio.data(), &numSamples, 0, 0) != 0) {
    return 2;
  }
  if (FILE *f = std::fopen("audio_regression_reference.raw", "wb")) {
    std::fwrite(audio.data(), sizeof(double),
                static_cast<size_t>(numSamples), f);
    std::fclose(f);
    std::printf("wrote audio_regression_reference.raw "
                "(%d samples, float64 mono @ %d Hz)\n",
                numSamples, audioSr);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (vtlInitialize(vtl_test::kSpeakerFile) != 0) {
    std::fprintf(stderr, "vtlInitialize failed for %s\n",
                 vtl_test::kSpeakerFile);
    return 2;
  }

  if (argc > 1 && std::strcmp(argv[1], "--dump-reference") == 0) {
    int rc = dumpReference();
    vtlClose();
    return rc;
  }

  bool ok = true;
  ok &= testTractToTube();
  ok &= testTransferFunction();
  ok &= testGesturalScoreToAudio();

  vtlClose();

  std::printf("\n%s\n", ok ? "OK" : "FAIL");
  return ok ? 0 : 1;
}
