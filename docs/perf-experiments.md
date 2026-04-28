# vtl synthesis performance experiments

Tracking ground for optimization work driven by the WASM-on-tablet stutter
investigation. Each experiment lists the hypothesis, what was changed, the
measured result, and whether it landed.

The numbers come from `synthesis_bench` (native) and `wasm_bench` (Node/V8),
both built from this tree. Re-baseline whenever the host machine changes —
a number from one machine isn't comparable to the same bench on another.

## TL;DR

E1 (Synthesizer tract cache) is the primary win: −50 % on
`BM_SynthesisAddTract_Held` in WASM, bit-exact regression test, and the
held-vowel cost converges to the per-sample TDS floor. E2/E3/E4 are
cleanups within the noise floor on this workload. Each experiment lives
on its own `perf/eN-*` branch; numbers below come from those branches
in isolation.

| Experiment | `BM_SynthesisAddTract_Held` (WASM) | Status |
|---|---:|:---:|
| baseline | 1634 µs | — |
| **E1** Synthesizer tract cache | **822 µs (−50 %)** | 🟢 |
| E2 Skip per-sample interpolate when prev==new | 1601 µs (−2 %) | 🟢 |
| E3 Hoist `prepareTimeStep` constants | 1646 µs (noise) | ⚫ |
| E4 Cache `factor*sqrt(factor)` in `calcNoiseSample` | 1655 µs (noise) | ⚫ |

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
| `BM_TractToTube` | 1.74 ms | 2.35 ms | 1.35× |
| `BM_GetTransferFunction` | 2.01 ms | 2.97 ms | 1.48× |
| `BM_SynthesisAddTube_Block` (120 samples) | 730 µs | 810 µs | 1.11× |
| `BM_SynthesisAddTract_Held` (120 samples, /a/) | 1317 µs | 1634 µs | 1.24× |
| `BM_GesturalScoreToAudio` (2.265 s audio) | 1240 ms | 1523 ms | 1.23× |

The held-vowel bench is the most informative one: the gap between
`BM_SynthesisAddTract_Held` (1634 µs WASM) and `BM_SynthesisAddTube_Block`
(810 µs WASM) — about 824 µs — is the per-chunk geometry cost
(`vocalTract->calculateAll()` + `getTube()`) plus the per-sample tube
interpolate. That's the budget the cache experiments target.

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

### 🟢 E1 — Cache the tube on Synthesizer when tract params don't change

**Hypothesis.** `vocalTract->calculateAll()` + `getTube()` is the dominant
cost in the held-vowel path and is deterministic w.r.t. tract params.
Caching the last computed Tube on the Synthesizer and reusing it when
`memcmp(newTractParams, cachedTractParams) == 0` eliminates this work
whenever the tract is static.

**Where.** `sources/vtl/synthesis/Synthesizer.cpp` (tract-param overload of
`addChunk`) + `sources/vtl/synthesis/Synthesizer.h` (cache fields).
Implemented at the Synthesizer layer (not AudioEngine) so every caller
benefits and the win is directly measurable in `BM_SynthesisAddTract_Held`.

**Branch.** `perf/e1-tract-cache`

**Result.** **🎯 Big win.**

| Bench | Native baseline | E1 native | Δ | WASM baseline | E1 WASM | Δ |
|---|---:|---:|---:|---:|---:|---:|
| `BM_SynthesisAddTube_Block` | 730 µs | 729 µs | ~0 % | 810 µs | 814 µs | ~0 % |
| `BM_SynthesisAddTract_Held` | **1317 µs** | **734 µs** | **−44 %** | **1634 µs** | **822 µs** | **−50 %** |
| `BM_TractToTube` | 1740 µs | 1724 µs | ~0 % | 2351 µs | 2356 µs | ~0 % |
| `BM_GesturalScoreToAudio` | 1240 ms | 1220 ms | −1.6 % | 1523 ms | 1454 ms | **−4.5 %** |

The held-vowel cost converges to `BM_SynthesisAddTube_Block` (per-sample
TDS only) on cache hits. On a tablet-class CPU this collapses the ~7 ms
`refillSynthRing` spike that was overrunning the worklet's 2.67 ms
quantum. `BM_GesturalScoreToAudio` improves a few percent on WASM because
the gestural score has stretches of constant params between transitions.

`audio_regression_test` passes **bit-exactly** — identical params take
the cache-hit path which copies the previously-computed Tube; the
cache-miss path is unchanged from before.

---

### 🟢 E2 — Skip per-sample `Tube::interpolate` when prev == new

**Hypothesis.** `Synthesizer::addChunk(double*, Tube*, ...)` calls
`tube.interpolate(&prevTube, newTube, ratio)` 120× per chunk. When
`prevTube == newTube`, all interpolations produce mathematically identical
sections.

**Where.** `sources/vtl/synthesis/Synthesizer.cpp::addChunk` (Tube* overload).

**Branch.** `perf/e2-skip-tube-interpolate`

**Result.** Small win, well below original expectations.

| Bench | Native baseline | E2 native | Δ | WASM baseline | E2 WASM | Δ |
|---|---:|---:|---:|---:|---:|---:|
| `BM_SynthesisAddTube_Block` | 730 µs | 725 µs | ~0 % | 810 µs | 813 µs | ~0 % |
| `BM_SynthesisAddTract_Held` | 1317 µs | 1300 µs | **−1.3 %** | 1634 µs | 1601 µs | **−2.0 %** |

The lerp inner loop is well-vectorized and bandwidth-bound; the upper
bound suggested by "103 sections × 4 ops × 120 samples" never materializes.
This is a follow-on cleanup to E1, not a primary optimization.

Drifts the regression hash by sub-ULP rounding (`a*ratio + a*(1-ratio)`
is not bit-identical to `a` even when `ratio + (1-ratio) == 1.0`
mathematically). Re-baseline if landing.

---

### ⚫ E3 — Hoist constants in `TdsModel::prepareTimeStep`

**Hypothesis.** Per-tube-section, per-sample `pow(REF_AREA_CM2/area,
AC_EXPONENT) * sqrt(AC_FREQUENCY_HZ)` simplifies to a constant divide
(`AC_R0_TIMES_SQRT_AC_FREQUENCY_HZ / area`) given AC_EXPONENT = 1.0 and
AC_FREQUENCY_HZ constant.

**Where.** `sources/vtl/core/Constants.h`,
`sources/vtl/acoustics/TdsModel.cpp::prepareTimeStep`.

**Branch.** `perf/e3-prepare-constants`

**Result.** **No measurable win.** LTO already folds `pow(x, 1.0)` and
hoists the sqrt of a const-folded argument.

| Bench | Native baseline | E3 native | Δ | WASM baseline | E3 WASM | Δ |
|---|---:|---:|---:|---:|---:|---:|
| `BM_SynthesisAddTube_Block` | 730 µs | 758 µs | within noise | 810 µs | 819 µs | within noise |
| `BM_SynthesisAddTract_Held` | 1317 µs | 1357 µs | within noise | 1634 µs | 1646 µs | within noise |

Drifts the regression hash sub-ULP. Cleanup-only; not worth landing on
its own. The DC pow rewrite (`pow(area, -2.6)` instead of `pow(1/area,
2.6)`) was tried first and reverted — same flatlined result, additional
hash drift.

---

### ⚫ E4 — Cache `factor*sqrt(factor)` in `calcNoiseSample`

**Hypothesis.** `factor * sqrt(factor)` (with `factor = 1000/cutoffFreq`)
is geometry-rate but evaluated per audio sample. Cache it alongside the
existing IIR-coefficient cache that already uses the same key.

**Where.** `sources/vtl/acoustics/TdsModel.{h,cpp}`.

**Branch.** `perf/e4-noise-sample`

**Result.** **No measurable win on this workload.**

| Bench | Native baseline | E4 native | Δ | WASM baseline | E4 WASM | Δ |
|---|---:|---:|---:|---:|---:|---:|
| `BM_SynthesisAddTube_Block` | 730 µs | 747 µs | within noise | 810 µs | 829 µs | within noise |
| `BM_SynthesisAddTract_Held` | 1317 µs | 1361 µs | within noise | 1634 µs | 1655 µs | within noise |

The held vowel /a/ produces almost no frication, so `currentAmp1kHz`
falls below the threshold (`MIN_MONOPOLE_AMP` / `MIN_DIPOLE_AMP`) and
`calcNoiseSample` early-returns at the top — the cached gain coeff path
is barely entered. Likely useful on a fricative-heavy workload; needs a
dedicated bench (e.g., a sustained /s/ or /sh/) to measure.

Also tried promoting `normal_distribution` to a static — works but
shifts the RNG sequence (the distribution carries internal state across
calls), drifting the regression hash and changing audio output. Reverted.

Drifts the regression hash sub-ULP from the multiplication reorder.
Cleanup-only; not worth landing on this workload.

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
