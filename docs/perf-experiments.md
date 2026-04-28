# vtl synthesis performance experiments

Tracking ground for optimization work driven by the WASM-on-tablet stutter
investigation. Each experiment lists the hypothesis, what was changed, the
measured result, and whether it landed.

The numbers come from `synthesis_bench` (native) and `wasm_bench` (Node/V8),
both built from this tree. Re-baseline whenever the host machine changes —
a number from one machine isn't comparable to the same bench on another.

## How to measure

```sh
# Native baseline. -fno-math-errno + IPO are on by default in Release.
cmake --build --preset mac-release --target synthesis_bench
./build/mac-release/synthesis_bench --benchmark_min_time=2s

# WASM under Node (V8). Requires emsdk on PATH (or EMSDK env).
cmake --preset wasm
cmake --build --preset wasm-bench
( cd build/wasm && node wasm_bench.js 2 )
```

Both report per-iteration time and an RTF (audio seconds produced per wall
second). RTF ≥ 1.0 is the real-time bar. The WASM bench uses
`emscripten_get_now()` — same clock the live AudioWorklet uses for its
deadline tracking, so the numbers are directly comparable to what the
worklet sees in production.

## Baseline (2026-04-28, Apple M-series, mac-release / Node v25 V8)

| Bench | Native | WASM (V8) | Slowdown |
|---|---:|---:|---:|
| `BM_TractToTube` | 1.92 ms | 2.39 ms | 1.24× |
| `BM_GetTransferFunction` | 2.10 ms | 3.11 ms | 1.48× |
| `BM_SynthesisAddTube_Block` (120 samples) | 761 µs | 837 µs | 1.10× |
| `BM_GesturalScoreToAudio` (2.265 s audio) | 1300 ms | 1546 ms | 1.19× |

WASM/V8 only adds 10–25% over native for the hot loops. The tablet stutter
is a *scheduling* problem, not a "WASM is slow at math" problem — see the
diagnosis below.

## Diagnosis (why the live build stutters on tablets)

`AudioEngine::refillSynthRing` (sources/live/AudioEngine.cpp:387) produces
480 samples on demand from the worklet, which pulls 128 samples per
callback. The synth ring is sized 2× chunk = 960 samples (~20 ms).

- 6 of every ~7 quanta hit the ring fast-path (just memcpy out).
- The 7th quantum hits an empty ring and runs a full `refillSynthRing`
  *inside* the worklet callback that has 2.67 ms to return:
  - `vocalTract->calculateAll()` + `getTube()` ≈ 2.4 ms WASM on M1, ~3.5 ms
    on a tablet-class CPU.
  - 480× per-sample TDS solve ≈ 3.4 ms WASM on M1, ~5 ms on tablet.

Result: a sawtooth load with a ~7-ms spike every ~17 ms inside a 2.67-ms
budget on tablets. That's the stutter.

`vocalTract->calculateAll()` runs every chunk regardless of whether tract
params changed (Synthesizer.cpp:135-142). In real interactive use the user
holds a slider for seconds at a time, so almost all of those calls
recompute an identical Tube. That's where the biggest, easiest win lives.

## Experiments

Status legend: 🟢 landed · 🟡 in progress · 🔵 proposed · ⚫ rejected

### 🔵 E1 — Cache the tube when tract params don't change

**Hypothesis.** `vocalTract->calculateAll()` + `getTube()` (~2.4 ms WASM on
M1, ~3.5 ms on tablet) is the dominant cost in `refillSynthRing` and is
deterministic w.r.t. tract params + the auto-tongue-root flag. Caching the
last computed Tube and bypassing `Synthesizer::addChunk(double*, double*,
...)` in favour of `Synthesizer::addChunk(double*, Tube*, ...)` on a
params-unchanged hit eliminates this work whenever no slider is moving.

**Where.** `sources/live/AudioEngine.cpp::refillSynthRing` and
`sources/live/AudioEngine.h` (cache fields).

**Implementation sketch.**

- Add to `AudioEngine`: `Tube cachedTube; double lastTractParams[NUM_PARAMS];
  bool lastAutoTongueRoot; bool tubeCacheValid;`.
- In `refillSynthRing`, after taking the snapshot:
  - If `tubeCacheValid` and `memcmp(snap.tractParams, lastTractParams, …) == 0`
    and `snap.autoTongueRoot == lastAutoTongueRoot`, call
    `synthesizer->addChunk(glottisParams, &cachedTube, SYNTH_CHUNK_SAMPLES, …)`.
  - Otherwise, run the existing path, then snapshot
    `vocalTract->getTube(&cachedTube)` into the cache and stamp the
    params/flag and set `tubeCacheValid = true`.
- Invalidate `tubeCacheValid` on `start()` and on speaker reload.

**Expected impact.**

- Steady-state (held vowel) chunk wall time drops from ~5 ms → ~2.5 ms on
  M1 WASM, ~7 ms → ~3.5 ms on tablet. Stutter mechanism eliminated for the
  common case.
- During slider drag the cache misses every chunk → no regression vs. today.
- `BM_GesturalScoreToAudio` won't move much (the gestural score keeps the
  tract in motion most of the time); the win is specifically in the live
  worklet path. Worth adding a synthetic "held vowel real-time" bench that
  drives `addChunk` with constant params for a measurable signal.

**Risks.**

- Anatomy mutation outside `calculateAll`. Currently anatomy is only
  swapped on speaker reload (which calls `restart()` and would hit the
  `tubeCacheValid = false` reset). Document this invariant in the cache
  field's comment.
- The audio-thread `vocalTract` is still mutated on cache miss; the UI
  thread holds a separate `uiVocalTract`, so no UI-side hazard.
- Determinism: `audio_regression_test` exercises gestural-score and
  static-phone paths that don't go through `AudioEngine::refillSynthRing`,
  so the test won't catch a bug in the cache. Add a focused round-trip
  test (or run a long held-vowel through the live engine and FNV-hash the
  output, comparing cache-on vs. cache-off).

**Result.** _(not yet run)_

---

### 🔵 E2 — Skip per-sample `Tube::interpolate` when prev == new

**Hypothesis.** `Synthesizer::addChunk(double*, Tube*, ...)` calls
`tube.interpolate(&prevTube, newTube, ratio)` 480× per chunk
(Synthesizer.cpp:195). When `prevTube == newTube` (the E1 cache-hit case,
or any quiescent run), all 480 interpolations produce identical sections.

**Where.** `sources/vtl/synthesis/Synthesizer.cpp::addChunk` (Tube* overload).

**Implementation sketch.**

- Detect equality once at top of the function (cheap memcmp on the
  fixed-size Section arrays, or a dirty bit set by E1 / addChunk's caller).
- On equality, copy `*newTube` into `tube` once and skip the per-sample
  interpolation; keep the per-sample loop body for `addSample`.

**Expected impact.** ~80–100 µs per 480-sample chunk (~3% of the chunk
budget). Pure win when E1 lands; small win without it.

**Risks.** Articulator field is selected per-sample by `ratio < 0.5`
(Tube.cpp:368-375); when prev == new this is a no-op. Safe.

**Result.** _(not yet run)_

---

### 🔵 E3 — Hoist constants in `TdsModel::prepareTimeStep`

**Hypothesis.** Per-tube-section, per-sample math at TdsModel.cpp:724-725
contains pow/sqrt calls that are mathematically equivalent to a single
divide. Compiler can't fold them because `AC_EXPONENT`, `REF_AREA_CM2`,
`AC_FREQUENCY_HZ` are non-`constexpr` `const double` (Constants.h:54-59).

- Line 725: `pow(REF_AREA_CM2/area, AC_EXPONENT) * sqrt(AC_FREQUENCY_HZ)` →
  with `REF_AREA_CM2 = 1.0`, `AC_EXPONENT = 1.0`, `AC_FREQUENCY_HZ = 2250`,
  this equals `sqrt(2250) / area = 47.43417… / area`. The pow + sqrt fire
  103 sections × 48000 Hz ≈ 5M times/s for nothing.
- Line 724: `pow(REF_AREA_CM2/area, DC_EXPONENT)` with `DC_EXPONENT = 2.6`.
  Making `REF_AREA_CM2` `constexpr` lets `1.0/area` fold; the `pow` itself
  remains.
- Line 752: `4.0*M_PI*pow((3.0*ts->volume)/(4.0*M_PI), 2.0/3.0)` for sinus
  sections. `ts->volume` only changes at geometry rate; cache the surface
  on the tube section (recompute in `setTube` / when area-rate changes).

**Where.** `sources/vtl/core/Constants.h` (mark constants `constexpr`,
add `AC_R0_TIMES_SQRT_F0_HZ` constant) + `sources/vtl/acoustics/TdsModel.cpp`.

**Expected impact.** 5–15% of `BM_SynthesisAddTube_Block`, every backend.
Most attractive on WASM where libm pow is heavier than native.

**Risks.**

- Numerical: `sqrt(2250.0)` evaluated at compile time vs. at runtime should
  be bit-identical for IEEE-754 round-to-nearest, but `audio_regression_test`
  hashes raw audio and will catch any drift. Re-baseline if it does.
- `pow(x, 1.0)` may already be optimized to `x` by libm or the compiler;
  the win is the eliminated sqrt, not the pow per se.

**Result.** _(not yet run)_

---

### 🔵 E4 — Cache `factor*sqrt(factor)` and static `normal_distribution`

**Hypothesis.** Two small per-sample wastes in `calcNoiseSample`
(TdsModel.cpp:1488):

- Line 1591: `factor * sqrt(factor)` where `factor = 1000.0/cutoffFreq`.
  `cutoffFreq` is geometry-rate, not sample-rate. Cache alongside the IIR
  coeffs that already use this pattern (TdsModel.cpp:1550-1573).
- Line 1603: `normal_distribution<double> normalDistribution(0.0,
  1.0/sqrt(12.0))` is constructed inside the function, called 3×/sample.
  Constructor includes the sqrt. Make it a static member; `M_2_SQRT3` is
  the inverse-sqrt-12 constant.

**Where.** `sources/vtl/acoustics/TdsModel.cpp` (and `.h` for the static).

**Expected impact.** 1–3% of `BM_SynthesisAddTube_Block`. Cheap, safe.

**Risks.** Static `normal_distribution` reuses internal state across calls
— that's actually preferred (it cuts the rejection-sample cost in half on
average) but must be confirmed deterministic against `audio_regression_test`.

**Result.** _(not yet run)_

---

### 🔵 E5 — Quantum-rate worklet chunks

**Hypothesis.** Once E1 is in, `calculateAll`/`getTube` per-chunk overhead
disappears, so chunk size only affects per-sample work. Sizing
`SYNTH_CHUNK_SAMPLES` to match the worklet quantum (128) flattens the
sawtooth load curve: every callback does the same amount of work, no spike.

**Where.** `sources/live/AudioEngine.h:236` and validate ring sizing
(`SYNTH_RING_CAP`) is still adequate.

**Expected impact.** No change to throughput; large change to *worst-case*
worklet callback time, which is what triggers the audible stutter.

**Risks.**

- Without E1, this would *increase* total work (more `calculateAll` calls
  per second). E5 must come after E1.
- Glottis state save/restore happens per chunk — verify it's not on the
  hot path.

**Result.** _(not yet run)_

---

### 🔵 E6 — Spatial acceleration for `getCrossProfiles`

**Hypothesis.** The README profile of `BM_TractToTube` shows ~75% of the
call in surface/triangle intersection geometry — `Surface::getEdgeIntersection`,
`prepareIntersection`, `getTriangleIntersection`, `getTriangleList`. The
current implementation does linear scans over triangle lists per
cross-section; a BVH or 3D grid would cut that to log N or near-O(1).

**Where.** `sources/vtl/anatomy/Surface.cpp`.

**Expected impact.** Up to 50% off `BM_TractToTube`. Only matters during
slider drag (when E1 misses). Larger refactor, real risk of breaking the
anatomy code that's been correct for ~20 years.

**Result.** _(deferred — pursue only if E1+E5 don't fully solve the
stutter on the worst tablet we care about)_

---

## Per-experiment workflow

1. Branch + implement.
2. Run `audio_regression_test` — it must still pass. If hashes drift,
   investigate before re-baselining.
3. Run `synthesis_bench --benchmark_min_time=2s` and `wasm_bench 2`,
   record under "Result" above as a delta vs. the baseline table.
4. If WASM ratio improves, that's a stutter-relevant win even if native
   stays flat.
5. Land or roll back; update status emoji.

## Known gaps in the bench coverage

- **Held-vowel real-time bench is missing.** None of the existing
  benchmarks measure the steady-state path that `AudioEngine::refillSynthRing`
  exercises (i.e., calling `addChunk(double*, double*, ...)` with constant
  params chunk after chunk). E1's win lands precisely there. Adding a
  bench that does `for N { addChunk(staticParams, staticParams, 480, …) }`
  would make E1 directly measurable.
- **No memory-bandwidth measurement.** WASM `-pthread + ALLOW_MEMORY_GROWTH`
  emits a warning about non-wasm code running slowly; if memory bandwidth
  turns out to be the cap, that's worth its own experiment.
- **Tablet measurement is by proxy.** All numbers above are from a desktop
  CPU. Confirm the tablet-class projection by running `wasm_bench` in
  Safari on the actual device once the live build can host a bench page
  (or by serving a dedicated bench shell).
