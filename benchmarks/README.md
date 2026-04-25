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
