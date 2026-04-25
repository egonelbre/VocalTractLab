# Synthesis pipeline optimization plan

Tracks profiling-driven optimization work on the VTL synthesis backend.
Baseline measurements and the per-idea implementation notes live here so
follow-up PRs can be reviewed against the same picture of where the time
goes.

## Baseline (M-series Mac, RelWithDebInfo, 2026-04-25)

Captured with `xctrace record --template 'Time Profiler'` against
`synthesis_bench` (10 s wall budget per benchmark). See `tools/profile.sh`
for the lighter `sample`-based variant.

### `BM_GesturalScoreToAudio` — RTF ≈ 1.7×, 2.265 s of audio in ~1.3 s

| %incl | %self | Symbol |
|---:|---:|---|
| 69 | — | `VocalTract::calculateAll` |
| 58 | 17 | `VocalTract::getCrossProfiles` |
| 13 | 13 | `Surface::appendTileTrianglesUnique` |
| 17 | — | `TdsModel::proceedTimeStep` |
| 27 | — | `Synthesizer::addSample` |

Geometry dominates the end-to-end cost.

### `BM_SynthesisAddTube_Block` — RTF ≈ 2× (per-block headroom)

| %incl | %self | Symbol |
|---:|---:|---|
| 92 | 4 | `Synthesizer::addSample` |
| 55 | — | `TdsModel::proceedTimeStep` |
| 37 | 10 | `TdsModel::prepareTimeStep` |
| 23 | — | `GeometricGlottis2025::calcGeometry` |
| 19 | 7 | `TdsModel::calcNoiseSources` |
|  9 |  9 | `TdsModel::solveEquationsCholesky` |
|  — |  2 | `IirFilter::clearCoefficients` (tell — see #2) |

Per-sample cost is split between the solver, the glottis geometry, and
noise generation.

### `BM_GetTransferFunction`

| %incl | %self | Symbol |
|---:|---:|---|
| 59 | — | `TlModel::getSpectrum` |
| 46 | — | `TlModel::prepareCalculations` |
|  — | **43** | **`__divdc3`** (libgcc complex-double division) |
| 23 | — | `TlModel::getSectionMatrix` |

Almost half the wall time is in a single libgcc complex-division helper.

---

## Ideas, in rough impact-vs-effort order

Each idea is sized to be a self-contained PR. Mark `[x]` when landed.

### [x] 1. Replace `1.0 / std::complex<double>` in TlModel

**Result (2026-04-25):** **2.31× speedup on BM_GetTransferFunction**
(4703 µs → 2035 µs, -57% wall). All audio-regression hashes
bit-identical — `cinv()`/`cdiv()` produce the same bits as libgcc's
`__divdc3` for the impedance regime here, so no hash update was
needed. Other benchmarks neutral (within stddev), as expected since
they don't exercise TlModel.



**Where:** `sources/vtl/acoustics/TlModel.cpp` lines 819, 868, 872, 873,
887–889, 916, 1085, 1099, 1153, 1171, 1181, 1226, 1263, 1303, 1373, 1386.

**Why:** `std::complex<double>` division compiles to a libgcc
`__divdc3` call (with NaN handling and an out-of-line trampoline).
That call alone is **43% self time** of the transfer-function
benchmark.

**How:** reformulate `a / b` as `a * conj(b) / norm(b)` where
`norm(b) = real*real + imag*imag`. Inlines to ~6 mul + 2 div, no
function call. The pattern `1.0 / b` simplifies further to
`conj(b) / norm(b)`. A small helper (`inline ComplexValue
inv(ComplexValue)` and `inline ComplexValue safe_div(...)`) keeps the
call sites readable.

**Risk:** the libgcc routine has special handling for NaN/inf inputs;
verify the impedance values stay in a regime where the simpler formula
is safe (real and imaginary parts both finite, denominator non-zero).
Lock the result with the existing audio-regression hash tests on
`vtlGetTransferFunction`.

**Expected:** 20–30% wall-clock win on `BM_GetTransferFunction`.

### [x] 2. Cache `IirFilter` per noise source

**Result (2026-04-25):** **-1.8% on BM_SynthesisAddTube_Block**
(762 µs → 748 µs). Audio hashes bit-identical.

First attempt embedded a full `IirFilter` (~1.6 KB) into each
`NoiseSource`; that **regressed** AddTube_Block by ~2.5% — the
unused `inputBuffer[64]` / `outputBuffer[64]` arrays inside
`IirFilter` blew out cache lines that the surrounding NoiseSource
fields share. Fix: cache only the small `cachedA[3]`, `cachedB[3]`,
`cachedOrder` (max filter order is 2). Use a stack `IirFilter` as a
scratch helper on the cache-miss slow path; the audio loop reads
the small cached arrays directly.

Win is modest because most `calcNoiseSample` calls during a held
vowel return early through the "source is off" branch; the cache
benefit only kicks in when noise is actively generating.



**Where:** `sources/vtl/acoustics/TdsModel.cpp:1540` (`calcNoiseSample`).

**Why:** a fresh `IirFilter` is constructed on the stack every audio
sample × number-of-noise-sources. Coefficients only depend on
`cutoffFreq * timeStep`, which is geometry-rate, not sample-rate.
`IirFilter::clearCoefficients` shows up at **1.8% self time** purely
because of this.

**How:** add an `IirFilter filter` member to `NoiseSource`, plus
`double cachedCutoffTimesStep`. In `calcNoiseSample`, recompute
coefficients only when the product changes (or use a dirty flag set
by the geometry update path).

**Risk:** none — pure code motion, no math change.

**Expected:** ~2–3% on `BM_SynthesisAddTube_Block`, plus removes the
allocation churn that hides further wins.

### [~] 3. Hoist constants and reformulate sqrt-filter in `calcNoiseSample` — **no measurable win, not committed**

**Result (2026-04-25):** tried hoisting `TIME_CONSTANT_SAMPLES`, `F`,
and `1.0 - F` to `static const` inside the `targetAmp > currentAmp`
branch. Bit-identical output, but no measurable wall-time
difference: the branch is only entered while noise amplitude is
*rising*, which is rare in `BM_SynthesisAddTube_Block` (held vowel
keeps both amplitudes at 0) and a tiny fraction of
`BM_GesturalScoreToAudio` overall. Reverted; the change wasn't worth
the noise floor it introduced. The sqrt-filter reformulation was
deliberately not attempted — the existing comment is explicit that
the sqrt is what gives plosives the right delayed-rise shape.



**Where:** `sources/vtl/acoustics/TdsModel.cpp:1500–1507`.

**Why:** `TIME_CONSTANT_SAMPLES` and `F = exp(-1/...)` are evaluated
every sample but are build-time constants. The `sqrt(amp) / square
back` filter doubles the work of the IIR step.

**How:** promote the two constants to file-scope `constexpr`; the
double-sqrt can be replaced with an equivalent direct `amp` filter
(verify the rise-time behavior matches — the comment says the sqrt is
deliberate for slower attack on plosives, so this needs a numeric
check before changing the math).

**Risk:** the sqrt change is *not* mathematically identical, so guard
behind audio-regression hashes; if the hash drifts, keep the sqrt
form and only land the constant hoisting.

### [ ] 4. Cache `GeometricGlottis2025::calcGeometry` between updates

**Where:** `sources/vtl/glottis/GeometricGlottis2025.cpp:153`,
called from `Synthesizer::addSample:248`.

**Why:** the function runs sqrt + sin + cos + exp + acos every audio
sample, but its output only changes when control parameters change
(typically every block, not every sample). **23% incl** of the
per-block benchmark.

**How:** add a "params dirty" flag on the glottis; `calcGeometry`
returns early with cached state when clean. Caller invalidates on
parameter set.

**Risk:** the glottis state evolves over time too (phase, pressure
integration). Need to separate slow-changing parameters (cached) from
sample-rate state (always recomputed). Likely a partial cache: hoist
sqrt(F0/restF0) and the constant prefactors but keep the per-sample
phase update in the hot path.

**Expected:** 5–10% on `BM_SynthesisAddTube_Block`.

### [ ] 5. Cache `Surface::prepareIntersection` / `getTriangleList` per articulation

**Where:** `sources/vtl/anatomy/VocalTract.cpp:5809–5814` (called inside
`getCrossProfiles`).

**Why:** the surface tessellation only changes when articulation
parameters change, but `prepareIntersection` and `getTriangleList`
run on every cross-section query. Combined Surface::* self time is
~26% of the gestural-score benchmark.

**How:** move the per-surface tile assignments to the geometry-update
phase (`VocalTract::calculateAll` or its callers); track a
generation counter so the cache invalidates correctly when articulation
changes. The `appendTileTrianglesUnique` stamp churn is downstream of
this and should drop too.

**Risk:** moderate — touches the geometry caching contract. Needs care
to invalidate on every path that mutates articulation.

**Expected:** 10–15% on `BM_GesturalScoreToAudio`.

### [ ] 6. Replace constant-exponent `pow()` in `prepareTimeStep`

**Where:** `sources/vtl/acoustics/TdsModel.cpp:718, 719, 746`.

**Why:** `pow(x, DC_EXPONENT)`, `pow(x, AC_EXPONENT)`, and
`pow(3*V/(4π), 2.0/3.0)` are evaluated every sample. If exponents are
known at compile time, branch on common cases or use `cbrt`/`sqrt`
directly. The sphere-surface `pow(..., 2.0/3.0)` reduces to
`cbrt(x*x)` and is a per-section-per-sample call.

**How:** replace with explicit forms after confirming the exponents.
For runtime exponents that change rarely, precompute per geometry
update.

**Risk:** low if exponent values are confirmed; medium if they're
configurable.

**Expected:** 1–3% on `BM_SynthesisAddTube_Block`, more on
`BM_GesturalScoreToAudio` if the geometry path also benefits.

### [ ] 7. Fuse post-processing passes in `getCrossProfiles`

**Where:** `sources/vtl/anatomy/VocalTract.cpp:5963–6080`.

**Why:** several sequential sweeps over the 96-sample profile array
(median clamping, gap fill, interpolation). Each pass evicts the
previous one from L1 on a hot inner function.

**How:** fuse into a single sweep where the dependencies allow. Some
passes are sequential by nature (a gap fill must see the clamped
result), so the fusion is partial.

**Risk:** low. Fold gradually and check the regression hashes between
each merge.

**Expected:** 2–5% on `BM_GesturalScoreToAudio`. Mostly a cache-locality
win.

### [ ] 8. Skip sqrt in `setupProfileLine` length check

**Where:** `sources/vtl/anatomy/VocalTract.cpp:6469`.

**Why:** `v.normalize()` (sqrt + 2 divides) gates a constant
`LENGTH_INC = 0.01` comparison. Squared-magnitude check skips the
sqrt entirely.

**How:** replace `length >= LENGTH_INC` with `lengthSquared >=
LENGTH_INC * LENGTH_INC`. If `v` is reused after the check, normalize
only on the kept branch.

**Risk:** trivial.

**Expected:** small per-call win, but called on every profile-line
intersection so it adds up.

---

## Validation methodology

Every PR should:

1. Re-run `synthesis_bench --benchmark_min_time=2s` before/after on the
   same machine and quote `BM_GesturalScoreToAudio`,
   `BM_SynthesisAddTube_Block`, and `BM_GetTransferFunction` numbers
   in the PR body.
2. Run `audio_regression_test` — synthesis output must stay
   bit-identical (or drift only by a documented amount, with hashes
   updated in the same PR).
3. For changes that touch math (#1, #3 sqrt-filter, #6), include a
   short numerical-stability note in the commit message.

## Updates

Re-profile after each batch of ideas lands; numbers shift as the
biggest costs come down and new ones become visible.
