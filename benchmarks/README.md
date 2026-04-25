# vtl synthesis benchmarks

Microbenchmarks for the `VocalTractLabApi` synthesis pipeline. Use these to
gauge how close the backend runs to real-time on a given platform / build
configuration, and where the time is going.

## Building & running

```sh
cmake --build --preset mac-release --target synthesis_bench
./build/mac-release/synthesis_bench --benchmark_min_time=2s
```

Optional flags worth knowing:

- `--benchmark_min_time=2s` — collect at least 2 s of samples per benchmark
  (default is 0.5 s).
- `--benchmark_filter=Block` — run only matching benchmarks.
- `--benchmark_format=json --benchmark_out=results.json` — capture for
  comparison with `compare.py` from the upstream `benchmark` repo.

The benchmarks always link against the `VocalTractLabApi` shared library and
load `data/speakers/JD2.speaker` + `data/example01.ges` from the in-tree
`data/` directory; paths are baked in at configure time via
`benchmarks/benchmark_data.h.in`.

## What each benchmark measures

| Benchmark | Path under test | What it tells you |
|---|---|---|
| `BM_GesturalScoreToAudio` | gestural score → tube sequence → TDS → audio | End-to-end throughput. Closest analogue to "synthesize a sentence offline". |
| `BM_SynthesisAddTube_Block` | one 2.5 ms TDS step (held vowel) | Steady-state real-time loop cost. Must finish in < 2.5 ms wall to keep up. |
| `BM_TractToTube` | geometry only (cross-sections, area function) | Cost of advancing the vocal tract shape. Called inside `vtlSynthesisAddTract`. |
| `BM_GetTransferFunction` | TlModel (frequency domain) | Independent code path; useful for analysis-style workloads. |

## Reading the RTF counter

The reported `RTF` (real-time factor) is **audio seconds produced per wall
second**. RTF ≥ 1.0 means the backend keeps up with playback.

- `BM_GesturalScoreToAudio` RTF — total throughput. RTF = 2.0 means a 1-second
  utterance synthesizes in 0.5 s.
- `BM_SynthesisAddTube_Block` RTF — per-block headroom. RTF = 3.0 means each
  2.5 ms block uses ~0.83 ms of wall time, leaving ~67 % of the budget for
  everything else in the audio thread.

## What the numbers mean for "real-time"

A real-time interactive synth has three things to do every audio block:

1. **Update the vocal tract shape** (somebody moved a slider, or a gesture
   target advanced). This is `vtlTractToTube` territory — relatively
   expensive but only needs to run when the shape *changes*.
2. **Run the acoustic step** for the new block. This is `vtlSynthesisAddTube`
   — must always run, must always be fast.
3. **Push the audio to the output device.** Outside the backend's scope.

If `BM_SynthesisAddTube_Block` already exceeds the 2.5 ms budget, no amount
of caching will save you and the acoustic solver itself needs work.

If `BM_TractToTube` is the bottleneck, you can probably amortize: cache the
last-computed tube and reuse it across many acoustic steps when the tract
shape is changing slowly (or interpolate at the tube level rather than the
tract level).

## Caveats

- Debug builds are typically 4–10× slower than Release. Always benchmark in
  Release mode for meaningful numbers; the build banner warns when Debug.
- macOS doesn't expose `hw.cpufrequency` so Google Benchmark can't print the
  CPU frequency — measurements are still accurate, only the metadata is
  affected.
- The benchmarks call `vtlInitialize()` exactly once per process (via a
  static fixture). Initialization time is not measured.

## Profiling

`tools/profile.sh` wraps Apple's `sample` command for quick CPU profiles:

```sh
tools/profile.sh BM_TractToTube              # default: 15 s of samples
tools/profile.sh BM_SynthesisAddTube_Block 30
```

The script (re)builds `build/mac-relwithdebinfo/synthesis_bench` with debug
info, runs the matching benchmark in the background, attaches `sample`, and
prints the per-function flat profile. macOS-only — on Linux use
`perf record / perf report`; on Windows use the VS profiler or VTune.

### Where the time goes (M-series Mac, RelWithDebInfo)

**`BM_TractToTube`** (~5 ms/call) — geometry-bound:

| % | Function |
|---:|---|
| ~26% | `VocalTract::getCrossProfiles` |
| ~21% | `Surface::getEdgeIntersection` |
| ~16% | `Surface::prepareIntersection` |
| ~9% | `Surface::getTriangleIntersection` |
| ~7% | `VocalTract::insertLowerProfileLine` |
| ~7% | `VocalTract::insertUpperProfileLine` |
| ~7% | `Surface::getTriangleList` |

**~75% of the call is in surface/triangle intersection geometry** —
slicing the 3D vocal-tract surfaces (tongue, palate, lips, …) by the
cutting planes that produce each cross-section profile. Speedup
candidates: spatial acceleration structure for the triangle lookup
(currently linear scan), SIMD on the inner intersection math, or
amortizing — only re-running affected cross-sections when a parameter
changes.

**`BM_SynthesisAddTube_Block`** (~750 µs/block) — solver-bound:

| % | Function |
|---:|---|
| ~46% | `TdsModel::solveEquationsCholesky` |
| ~11% | `TdsModel::prepareTimeStep` |
| ~7% | `TdsModel::calcNoiseSample` |
| ~6% | `pow` (libm, scattered) |
| ~5% | `TdsModel::calcMatrix` |
| ~4% | `TdsModel::updateVariables` |

**Almost half the budget is the per-step Cholesky solve** of the
tube-section linear system. Speedup candidates: replace with a
specialized banded/tridiagonal solver, vectorize the substitution
loops, or reduce the internal oversampling rate.
