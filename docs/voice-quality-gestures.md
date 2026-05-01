# Voice-quality gestures: twang, widening, and the orthogonal control surface

Tracking ground for redesigning the supraglottal control surface so each
slider corresponds to one independent volitional gesture, not a perceptual
bundle. The two driving inputs are recent MRI/CT/aerodynamic literature on
vocal twang and on pharyngeal widening in singing; the central observation
is that the current `MCP`/`MCO` pair conflates several physiologically
independent muscle actions, and that the *widening* axis is missing
entirely.

## TL;DR

- The current twang implementation is a single bundled `MCP` (AES + AP
  epiglottis shift + ML pinch + piriform shrink) plus an `MCO` that
  half-implements pharyngeal narrowing (no widening direction at all).
- Real singing decomposes into ~13 orthogonal volitional gestures. Most
  already have clean params (`HX/HY/JX/JA/LP/LD/VS/VO/TCX/TCY/TBX/TBY/
  TRX/TRY/TS1–3` plus the Glottis model). The supraglottal extras needed
  are: an `AES` atom (pure sphincter), a `TT` atom (thyroid tilt), and a
  signed `PW` (pharyngeal width: stylopharyngeus dilation on the +
  side, constrictor engagement on the − side).
- This is not a new acoustic model — it's a clean separation of effects
  inside `VocalTract::calcCrossSections`. Wall compliance / source-filter
  coupling stays out of scope (separate experiment).

## Why orthogonality matters

A "twang" preset blends ~6 independent gestures (AES contraction, slight
laryngeal elevation, mouth opening, tongue dorsum lifted/forward, sealed
velum, and — in CVT "Distinct" / Edge — extra epiglottic tilt). A "squillo"
preset blends an *opposite* combination (AES contraction, lowered larynx,
**widened** pharynx, lowered epiglottis via thyroid tilt). If the controls
are perceptually labelled ("Twang slider") instead of mechanically labelled
("AES sphincter"), the same anatomical knob has to mean different things in
different presets, and the synth's parameter space stops being navigable.
The Mainka 2015 result (speech → classical singing changes the hypopharynx
+21.9% area while the AES doesn't change at all) is the cleanest
demonstration that AES and pharynx are mechanically independent and need
to be controlled independently.

## The orthogonal gesture set

These are gestures a singer can vary while holding the others fixed. Each
corresponds to a distinct muscle group or articulator, not a perceptual
quality.

| # | Gesture | Muscles / mechanism | Acoustic primary effect | Current param |
|---|---|---|---|---|
| 1 | AES sphincter contraction | Oblique interarytenoids + aryepiglottic m. | Narrows ET → epilarynx as quarter-wave resonator (~3 kHz cluster) | `MCP` (conflated) |
| 2 | Thyroid forward tilt | Cricothyroid (CT) | Stretches folds (F0); secondary epiglottis depression | — *missing* |
| 3 | Pharyngeal width (signed) | Stylopharyngeus + palatopharyngeus (dilate) ↔ sup./mid./inf. constrictors (narrow) | Hypopharyngeal volume; F1; ratio for singer's-formant cluster | `MCO` (only narrows; no widen) |
| 4 | Larynx vertical position | Supra- vs infrahyoid strap m. | Tract length → all formants scale | `HY` ✓ |
| 5a | Velar height (raise / lower) | Levator veli palatini, palatoglossus | Closes / opens VP port | `VS` ✓ |
| 5b | VP port aperture | Superior constrictor + velar contact | Coupling magnitude to nasal cavity | `VO` ✓ |
| 5c | Velar tension / stiffness | Tensor veli palatini, musculus uvulae | Damping of nasal-coupled modes | — *missing (no slider, fixed by anatomy XML)* |
| 5d | Lateral VP port pattern | Lateral velar elevation asymmetry | Partial / lateral coupling (singer leaks) | — *missing (port modelled as a single area)* |
| 6 | Tongue body height | Genioglossus posterior, styloglossus | F1 | `TCY` ✓ |
| 7 | Tongue body advancement | Genioglossus anterior | F2 | `TCX` ✓ |
| 8 | Tongue root | Hyoglossus, styloglossus | F1, hypopharyngeal volume | `TRX/TRY` ✓ (currently also driven by `MCO`) |
| 9 | Jaw aperture | Masseter / digastric | F1, mouth-end radiation | `JA`, `JX` ✓ |
| 10 | Lip aperture | Orbicularis oris | F1/F2; radiation impedance | `LD` ✓ |
| 11 | Lip protrusion / retraction | Orbicularis + risorius / zygomaticus | Effective tract length at the mouth end | `LP` ✓ |
| 12 | Glottal adduction / CQ | LCA, IA | Source spectrum (H1–H2) | Glottis model ✓ |
| 13 | Subglottal pressure / F0 | Respiratory + cricothyroid | Source level, pitch | Glottis model ✓ |

**Result:** 9 of 13 are already clean. Three need work (#1, #2, #3) and
one — tongue root (#8) — is fine on its own but needs `MCO` to stop
pushing it.

## What the current implementation actually does

Two parameters drive everything: `MCP` (AES/epilarynx, range 0..1) and
`MCO` (oropharynx, range 0..1). Neutral is 0. Defined in
`sources/vtl/anatomy/VocalTract.cpp:174–175`, exposed in
`sources/live/ControlsPanel.cpp:146–148`.

Effects, all keyed off those two scalars:

1. **Area-function scaling** (`VocalTract.cpp:5628–5695`). Two adjacent
   Hann windows along the centerline:
   - AES: 0.5–3.0 cm above glottis.
   - Oropharynx: 3.0 cm to `nasalPortPos − 0.5 cm`.
   Each scales `area` and `circ` (∝ √area) down to 50% at the centre of its
   window (`medialCompressionParamToFactor`,
   `VocalTract.cpp:4344–4360`).
2. **AP epiglottis shift** (`5708–5753`): translates the entire epiglottis
   surface in −x by up to 5 mm, scaled by the AES Hann.
3. **Tongue-root retraction** (`5761–5787`): pushes back-of-tongue ribs
   along their normal by up to 6 mm, scaled by the oropharynx Hann.
4. **Mediolateral pinch** (`5789–5848`): scales z down to 50% on
   UPPER_COVER, LOWER_COVER, EPIGLOTTIS within both windows.
5. **Piriform fossa volume** (`7662–7683`): linearly down to 50% with
   `MCP`; length unchanged.

That's it. Nothing else moves: no jaw, no lip, no tongue body, no larynx
height, no velum, no glottal source.

## Background: biological gestures of twang

The defining physiological hallmark is active narrowing of the
aryepiglottic sphincter (AES). Quantitatively (CT-based, Yanagi et al.):

- AES cross-section drops from baseline ~0.36 cm² to ~0.20 cm² (~44%
  reduction) at a standard plane 1.2 cm above the glottis.
- Total epilaryngeal-tube volume drops 50.34% in low-pitch twang (B2,
  ~1919 → 952.8 mm³) and only 21.26% in high-pitch twang (G4, ~1919 →
  1511 mm³). The mechanical constraint at high pitch (CT-stretch +
  laryngeal elevation) prevents full circumferential collapse.
- Narrowing occurs in both AP and ML directions; ML reduction is often
  larger.

Other co-articulating gestures (PDF §"Megaphone-like configuration"):

- Slight to moderate laryngeal elevation → shorter tract.
- Wider mouth (jaw down, lip retraction) → "bell" of the megaphone.
- Higher / more forward tongue body.
- Velum sealed (Oral Twang) or lowered (Nasal Twang). Same AES gesture in
  both — they differ only in velar position.

**Pedagogy splits**:

- **EVT** (Estill): lowercase *twang* = the AES ingredient; capital
  *Twang* = a specific Voice Quality (AES + high larynx + thyroid tilt +
  high tongue), with Oral / Nasal sub-variants.
- **CVT**: classifies by epiglottic-funnel compression. *Necessary twang*
  = arytenoids approximate to epiglottic petiole (moderate). *Distinct
  twang* = epiglottis body tilts back to nearly touch arytenoids
  (extreme). The latter is mandatory for "Edge" mode.

**Acoustic claim** (Titze): when the ET cross-section is narrow enough to
make the area ratio between the constricted ET and the immediately
superior pharynx ≥ 1:6, the ET behaves as an independent quarter-wave
resonator at ~2.8–3.0 kHz, clustering F3–F5 into the singer's-formant
band. *In vivo* MRI studies show the actual ratio in Operatic singing is
~4.6, not 6 — formant clustering doesn't strictly require the full
theoretical ratio.

## Background: pharyngeal widening

Pharyngeal width is an independent axis driven by a different muscle
group. The relevant biology:

- **Constrictors** (sup./mid./inf., CN X) narrow the pharynx at three
  vertical levels.
- **Dilators**: stylopharyngeus (CN IX) actively pulls the lateral
  pharyngeal walls outward and dorsally; palatopharyngeus and
  salpingopharyngeus assist. Stylopharyngeus naturally co-elevates the
  larynx, but trained singers dissociate this with infrahyoid co-
  contraction.
- **Passive expansion**: pharyngeal walls are compliant
  (~5.1–8.5 × 10⁻⁵ cm³/dyn, Ohala & Riordan 1979). High supraglottal
  pressure dilates the pharynx mechanically — this is the basis of SOVTE
  (straws, lip trills, etc.).

Quantitative pharyngeal widening from MRI:

- **Speech → classical singing** (Mainka 2015): vertical larynx down
  8 mm, hypopharynx +21.9% area, +16.8% volume, **AES dimensions
  unchanged**. PW and AES are operationally orthogonal.
- **Pitch vs SPL** (Echternach 2016): F0↑ → larynx↑ + lips/jaw open.
  SPL↑ → larynx↓ + pharynx widens + lips open. Pharynx width correlates
  with SPL more than pitch.
- **Cross-style at C5** (Höglund/Lindblad/Sundberg 2024):

| Style | Aph (mm²) | Ae (mm²) | Aph/Ae | Larynx |
|---|---:|---:|---:|---|
| Western Operatic | 440.9 ± 5.5 | 96.6 ± 4.7 | 4.6 | lowest |
| Kulning (herding call) | 185.7 ± 27.7 | 63.1 ± 2.2 | 2.9 | elevated |
| Edge (CCM/twang) | 154.4 ± 19.2 | 57.5 ± 0.4 | 2.7 | highest |

This is the cleanest evidence that "twang" styles (Edge, Kulning) actively
*narrow* the pharynx, while Operatic widens it. The 1:6 area ratio is
theoretical, not measured even in elite Operatic.

## Background: velum & nasal coupling

Velar control is already partially exposed (`VS`, `VO` are clean
parameters), but the nasal *side* of the system — what happens once the
VP port opens — is much less developed than the supraglottal tract. The
two PDFs both treat velar position as the primary differentiator between
Oral and Nasal twang, so even though no slider work is required for
stages 1–5, this section captures everything that would be needed for
a faithful nasal pathway.

### Velar anatomy (relevant muscles)

| Muscle | Action on velum | Innervation |
|---|---|---|
| Levator veli palatini | Raises and retracts the velum (closes VP port) | Vagus / pharyngeal plexus (CN X) |
| Tensor veli palatini | Tenses the velum, opens auditory tube | Trigeminal mandibular (CN V₃) |
| Palatoglossus | Lowers velum / raises tongue dorsum (closes oropharyngeal isthmus) | Vagus (CN X) |
| Palatopharyngeus | Lowers velum, narrows pharyngeal isthmus, shortens pharynx | Vagus (CN X) |
| Musculus uvulae | Shortens & stiffens the uvula, helps complete VP closure | Vagus (CN X) |
| Superior constrictor (Passavant's ridge contribution) | Anterior bulging of posterior pharyngeal wall to meet velum | Vagus (CN X) |

The functionally independent velar gestures a singer can vary are:

1. **Velar height** — primary raise/lower (already `VS`).
2. **VP port aperture** — magnitude of opening when not fully sealed
   (already `VO`).
3. **Velar tension / stiffness** — affects how strongly nasal modes are
   damped. Not currently a control input; tissue properties are baked
   into the anatomy XML.
4. **Closure pattern** — coronal (full circumferential), sagittal,
   lateral (lateral ports leak while center seals). Trained singers
   exploit lateral leaks for "controlled nasality". The current model
   collapses this to a single port area.
5. **Uvular position** — small but distinct in some genres
   (e.g. uvular trill, French uvular /ʁ/, "throat singing" overtones).
   Currently determined by the velum interpolation, not independently
   controlled.

### Acoustic effects of nasal coupling

When the VP port opens, the nasal pathway acoustically couples to the
oral pathway as a parallel side-branch:

- **Pole–zero pairs (anti-resonances)**: the nasal cavity introduces
  fixed zeros into the transfer function. The dominant ones in adults
  are typically near 500 Hz, 1000 Hz, 1500–2000 Hz, and 2500 Hz, but
  the precise frequencies depend on individual nasal cavity geometry
  and paranasal sinus volumes. These zeros are what make nasalised
  vowels acoustically distinct, not an "extra resonance" being added.
- **Maxillary sinus zero** (~150–400 Hz): the maxillary sinus is a
  Helmholtz resonator (~15–20 ml volume, 3–5 mm ostium) that drains
  into the nasal cavity. It produces a very narrow, very low-frequency
  zero that's audibly the "honk" of nasal vowels.
- **Frontal & sphenoidal sinus zeros**: smaller, higher (~200–600 Hz),
  more variable per individual.
- **Ethmoid air cells**: distributed damping rather than discrete
  zeros — they widen formant bandwidths.
- **F1 lowering and broadening**: open VP port couples a long compliant
  side-branch, dragging F1 down and increasing its bandwidth. This is
  why nasal vowels "feel softer" even at the same SPL.
- **Two-port radiation**: sound radiates from both the lips and the
  nostrils. Far-field directivity differs between them; phase
  combination matters at higher frequencies.
- **Nasal murmur**: when the oral cavity is sealed but the VP port is
  open (e.g. /m/, /n/), a low-frequency hum (~250–300 Hz) radiates
  from the nostrils; a single nasal pole dominates.

### Current code path

- `VS` (Velum Shape, 0..1, neutral 0): interpolates between three
  hand-mapped key states (low / mid / high) defined per-anatomy in
  `anatomy.velumLowPoints` / `velumMidPoints` / `velumHighPoints`
  (`VocalTract.cpp:899–1020`).
- `VO` (Velic Opening, cm², range −0.1..1.5): explicit port area passed
  through to the Tube model via `nasalPortArea_cm2`.
- Nasal cavity in the Tube model: a single uniform-section tube whose
  length is read from the anatomy XML
  (`anatomy.nasalCavityLength_cm`). No area variation along its
  length, no side branches, no per-individual variation beyond length.
- Paranasal sinuses: not modelled. There is a piriform-fossa side
  branch in `getTube` (`VocalTract.cpp:7662–7683`) but no analogous
  maxillary / frontal / sphenoidal / ethmoidal branches.
- Radiation: lip-only. There is no nostril radiation port; nasal
  energy that enters the side-branch is absorbed into the nasal-tube
  termination rather than radiated separately.
- Velum tension: not exposed; the velum is treated as a rigid surface
  whose only state is its shape.

### What the radiated lip output actually needs (Vampola 2020 update)

Vampola, Horáček, Radolf, Švec & Laukkanen 2020 ran a CT-derived 3D
FE model + 3D-printed physical model + in-vivo measurement on the
same female subject for nasalised /a:/ and /i:/. Their conclusions
collapse most of the nasal-pathway elaborations below into a single
high-impact lever and demote the rest:

- **With realistic wall damping (α = 0.02), every paranasal-sinus
  inner-mode peak vanishes from the radiated lip spectrum.** The
  combined audible effect of nasalisation at the lips reduces to
  F1 –2.4 dB, F1 frequency +36 Hz, F3 frequency +83 Hz (their
  Figs. 9 & 10). The 3D-printed physical model and the in-vivo
  recording both show only **one** cumulative nasal feature below
  F1, not the multiple sinus pairs that show up in the undamped
  FE solver.
- The maxillary-pair Helmholtz peaks (250/277 Hz) and sphenoid
  pair (416/452 Hz) are the dominant *internal* nasal modes, but
  Havel et al. 2017 (cited by the paper) show the sinuses **absorb
  rather than amplify** sound under normal conditions.
- The sinus modes do appear strongly at the **nostril** and as
  **wall vibrations** — likely the physiological basis for the
  singer's "facial mask" / "head resonance" sensation. Neither of
  these contributes to the lip-radiated sound at typical listening
  distance.

So the minimum-viable nasal-modelling plan for a 1D area-function
synth that wants better nasal vowels is much shorter than the
original list:

1. **Velar tension parameter** (`VT`, Task #12). Damping multiplier
   on the nasal-tube wall losses. Vampola's results pin α ≈ 0.02
   as the transition value where individual nasal modes become
   inaudible at the lips — `VT` controls how much of the nasal
   complexity reaches the radiated output. **Single highest-impact
   change.**
2. **Nostril radiation port** (Task #15). Second radiation
   termination with its own impedance, mixed into the output with
   a calibrated gain. Captures the audibility of nasal /m/, /n/
   and any close-mic recordings where nostril output matters.
   The current lip-only radiation never picks up the nostril modes
   even when the VP port is wide open.
3. **Lateral / asymmetric VP port** (Task #17). Two-component port
   for the trained-singer "leak" gesture.

### Items dropped from the parked plan

The Vampola results show these have negligible impact at the lips
and aren't worth the implementation cost for a 1D synth:

- ~~**Variable-area nasal cavity**~~ (was Task #13). Once damping
  is realistic, the nasal-cavity area function barely affects the
  *radiated* spectrum. Geometry mostly matters for the wall-vibration
  sensation that the area-function model can't represent anyway.
- ~~**Maxillary sinus side-branch**~~ (was Task #14, *deleted*). With
  realistic damping the maxillary Helmholtz contributes a notch of
  only **~1–3 dB at ~260 Hz** at the lips — measurable on a spectrum
  analyser, just-audible-to-inaudible in casual listening. The
  3D-printed physical model and in-vivo recording both replace the
  paired sinus peaks with a single broad cumulative dip below F1,
  which the existing single-tube nasal coupling already approximates.
- ~~**Frontal & sphenoidal sinus side-branches**~~ (was Task #16,
  *deleted*). Even smaller contributions than the maxillary; the
  paper's frontal-sinus modes (650 / 845 Hz for /a:/) are
  effectively invisible at the lips after damping.

### Other parked items that still make sense

These don't affect the lip-output much either, but cost very little
and unlock specific use cases:

- **Uvular position as an independent gesture** (`UV` split off
  from `VS`). For uvular trill, certain throat-singing styles.
- **Dynamic velar timing**. Velar transitions 50–200 ms with leak
  during transitions. Matters for diphthongs / consonant
  articulation in speech, less for sustained singing.
- **Mucosal congestion modulator**. Single multiplier on nasal-
  cavity area + wall losses → sick-day voice. Trivial once `VT`
  is in place.
- **Wall-vibration / bone-conduction model**. Out of reach for an
  area-function synth; would require a coupled mechanical-acoustic
  solver. The paper itself flags this as the actually-interesting
  role of the sinuses for singers' subjective experience.
- **Source-side velar-elevation effect**. Velum drop pulls on the
  levator/superior-constrictor sling, tightening the pharynx
  slightly. Tiny.

### Velum gaps in the gap table

Severity column re-evaluated against Vampola 2020 — what actually
shows up at the radiated lip output vs what only matters at the
nostril or in wall vibrations.

| Mismatch | What the literature says | Current code | Severity at the lips |
|---|---|---|---|
| Velar tension fixed | Wall damping α determines whether nasal modes are audible at all | Anatomy XML constant | **High** — controls F1 bandwidth + whether sinus features reach the lips |
| No nostril radiation | Nasal /m/, /n/ radiate from nostrils; close-mic recordings need it | Lip-only radiation | Med |
| Single VP port area | Real port has central + lateral components | Single scalar `VO` | Low |
| Nasal cavity is a uniform tube | Real cavity has area variation but radiated spectrum is dominated by F1 coupling, not internal nasal-tube shape | Single section in `Tube::initStaticSections` | **Low** (was Med — Vampola shows damped inner-tube modes don't reach the lips) |
| No paranasal sinuses | Maxillary / sphenoid pairs visible at nostril, ≤2 dB notch at lips | No side-branch on the nasal tube | **Low** (was Med — Vampola: realistic damping erases sinus peaks at lips) |
| Uvula not independent | Some genres need it | Coupled to `VS` | Low |
| Oral / Nasal twang preset binding | PDF makes this the primary twang dichotomy | `VS`/`VO` are independent of `MCP` | **Cosmetic / preset-only fix** |

The last row is the one already addressed by Stage 5 of the main
plan (the Oral-twang vs Nasal-twang presets simply write different
`VS`/`VO` values together with the AES atom). The "High" severity
row (`VT` velar tension / wall damping) is the highest-leverage of
the parked items and is the primary reason the sinus side-branch
work is no longer worth doing.

## Where the current implementation falls short

| Mismatch | What the PDF says | Current code | Severity |
|---|---|---|---|
| Pharyngeal *widening* missing | Operatic +21.9% hypo area; SOVTE; soprano formant tuning | `MCO` only narrows | **High** — half the genre space is unreachable |
| AES bundles thyroid tilt | Tilt is a separate gesture (CVT, EVT lowercase-uppercase distinction) | Epiglottis AP shift fused into `MCP` | High — Belt vs Squillo distinction lost |
| AES bundles pharyngeal pinch | AES is local; pharyngeal squeeze is separate | ML pinch on UPPER/LOWER_COVER spans full window | Med |
| Tongue root double-driven | TR is its own articulator | `MCO` re-pushes back-of-tongue ribs | Med — fight with TRX/TRY |
| Pitch-independent constriction | High-pitch twang only ~21% volume reduction; low-pitch ~50% | Same factor at any F0 | Med |
| Absolute area target absent | AES cross-section ~0.20 cm² | Multiplicative 50% factor of whatever baseline is | Med |
| Volumetric reduction undershoots | Low-pitch 50%, code Hann-50% area = ~25% volume | Hann profile + 50% floor | Low |
| Epiglottis translates not tilts | CVT Distinct twang: epiglottis body *tilts back* | Uniform −x translation | Low (visual mostly) |
| Arytenoids don't move | AES is arytenoid-anterior + epiglottis-posterior | Only the front wall closes in | Low |
| Vallecula not modelled | PDF: vallecular volume reduction matters | No side-branch | Out of scope (1D model) |
| No source-filter coupling | Pth drops parabolically as Ae → 0; +10–15 dB free gain | TDS solver is linear; Glottis runs independently | Architectural — separate experiment |
| No wall compliance | SOVTE / passive expansion | Walls are rigid in solver | Architectural — separate experiment |
| UI label overloads "twang" | Twang ≠ MCO; MCO is the megaphone hypopharyngeal squeeze | "Twang / medial compression" header bundles them | Cosmetic but misleading |

## Proposed control surface

Replace `MCP`/`MCO` with three orthogonal atoms. Keep all other params
unchanged. Presets sit on top as a UI layer; they don't add state.

### `AES` — sphincter contraction (range 0..1)

Drives, and only drives:

- AES centre-line area target. Multiplicative factor + absolute clamp
  toward ~0.20 cm² when AES = 1.
- AP narrowing of the **arytenoid**-region posterior wall.
- AP narrowing of the **epiglottic petiole**-region anterior wall.
- ML pinch *only* within the AES Hann window (0.5–3.0 cm above glottis).
- Piriform fossa volume reduction (passive coupling, kept).

Nothing above the aryepiglottic folds. Nothing on the tongue. Nothing on
the larynx height.

Optional pitch-aware scaling: full strength at ≤B2 / ~123 Hz, reduced to
~40 % at G4 / ~392 Hz, to track the volumetric data.

### `TT` — thyroid forward tilt (range 0..1, *new*)

Drives:

- Forward rotation of the thyroid cartilage anchor.
- Backward tilt of the upper EPIGLOTTIS surface around its inferior
  anchor (this is the "Distinct twang" gesture currently fused into
  `MCP`).
- Small shortening of the epilaryngeal tube length window used by `AES`.

Independent from AES. CVT/EVT pedagogy treat this as a primary axis: it's
the difference between Belt (no tilt) and Squillo / Opera (tilt).

### Coupling between `TT` and the Glottis source

Cricothyroid (CT) action — what `TT` represents — physiologically does
two things at once: it tilts the thyroid cartilage forward (cartilage
geometry) **and** stretches/thins the vocal folds (source). The
codebase's solver split (`VocalTract` and `Glottis` are independent
models with separate `controlParam[]` vectors) means we have a choice
about where to model that bundle.

The model already has one CT-related coupling baked in: in
`GeometricGlottis2025.cpp:171` and `:406`,
`length = REST_LENGTH * sqrt(FREQUENCY / REST_F0)`. So when the user
raises `FREQUENCY`, the folds stretch. What's missing is the
*tilt-without-pitch-change* path that distinguishes e.g. Squillo from
Belt at the same F0.

**Decision: keep `TT` orthogonal at the param level; bundle source
effects through the preset layer.**

- `TT` writes only cartilage geometry (thyroid forward rotation →
  epiglottis tilt + AES window shortening) as planned in Stage 2.
  No direct touches into the Glottis solver.
- Voice-quality presets (Stage 5) write both `snap.tractParams` and
  `snap.glottisParams`. The source-side adjustments live there.
  Because vowel/consonant clicks only affect `tractParams`, the
  glottal source colouring persists across vowel changes without any
  extra skip-list logic on the Glottis side.
- Typical Glottis-side adjustments per preset (CT-correlated):
  - **Squillo / Soprano / Distinct twang** (high TT): higher
    `RELATIVE_AMPLITUDE` damping → lighter spectrum, slightly
    tighter `LOWER_END_X / UPPER_END_X` (firmer adduction), longer
    `PHASE_LAG` for richer first-harmonic structure.
  - **Belt / Edge** (no TT): lower `RELATIVE_AMPLITUDE` damping →
    brighter, harmonically rich source, firm adduction without
    extra spectral tilt.
  - **SOVTE**: small adduction relaxation; the back-pressure does
    the work of keeping the folds adducting cleanly.

**Out of scope (parked Stage 6).** A `TFC` (tilt-fold coupling)
parameter that bleeds `TT` directly into Glottis derivations
(length / thickness biases, pulse-shape softening) for users who
want a single combined knob. Default 0 keeps the orthogonal
behaviour. Modelled after CVT's "thick vs thin folds" axis. Skip
until presets demonstrate the need.

### `PW` — pharyngeal width (range −1..+1, *signed, replaces `MCO`*)

Acts only between the AES top (3 cm) and the velar port. Centroid of the
window biased toward the *hypopharyngeal* end (asymmetric Hann), because
that's where Mainka measured most of the speech→classical change.

- **Negative** (constrictor engagement): centerline area scaled down,
  ML pinch on UPPER_COVER + LOWER_COVER pharyngeal/throat ribs, no
  effect on tongue root. Magnitude calibration: PW = −1 ≈ −40%
  hypopharyngeal area (covers Edge / Kulning).
- **Positive** (stylopharyngeus dilation): centerline area scaled *up*,
  ML expansion (z > 1) on the same ribs, slight tongue-root advancement,
  soft clamp against `getPharynxBackX` and the tongue body. Magnitude
  calibration: PW = +1 ≈ +50% hypopharyngeal area.

`TRX/TRY` keeps full ownership of the tongue root. `MCO`'s back-of-tongue
push goes away.

### `VS` / `VO` — velum (unchanged for stages 1–5)

Velar atoms stay as-is. Stage 5 presets bind them as part of voice-
quality recipes (Oral twang seals the port, Nasal twang opens it
~0.5 cm²). Future velum work is parked in the section above and tracked
as separate Stage 6 tasks.

### Presets (UI-only — no new state)

| Preset | AES | TT | PW | HY (Δcm) | JA (Δdeg) | LD (Δmm) | TCY (Δ) | VS | VO (cm²) | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Neutral speech | 0.0 | 0.0 | 0.0 | 0 | 0 | 0 | 0 | 0.5 | 0.0 | baseline |
| Necessary twang (CVT) | 0.3 | 0.0 | −0.1 | 0 | 0 | 0 | 0 | 0.8 | 0.0 | healthy default, sealed |
| Distinct twang (CVT) | 0.8 | 0.5 | −0.2 | +0.4 | −1 | +1.5 | −0.3 | 0.9 | 0.0 | quack / cackle, sealed |
| Edge / Belt (CCM) | 0.7 | 0.0 | −0.4 | +0.5 | −2 | +2 | −0.3 | 0.9 | 0.0 | Aph≈154 mm², ratio 2.7, sealed |
| Kulning | 0.6 | 0.2 | −0.3 | +0.3 | −2 | +2 | −0.2 | 0.9 | 0.0 | Aph≈186 mm², ratio 2.9, sealed |
| Operatic squillo | 0.5 | 0.7 | **+0.7** | −0.8 | −3 | +1 | −0.1 | 0.9 | 0.0 | Aph≈441 mm², ratio 4.6, sealed |
| Soprano formant-tune | 0.3 | 0.5 | +0.5 | +0.2 | −5 | +3 | −0.5 | 0.85 | 0.0 | high-F0 lift, sealed |
| SOVTE / straw | 0.0 | 0.2 | +0.4 | −0.2 | 0 | LD≈0 (lip occlusion) | 0 | 0.7 | 0.0 | passive widening pattern |
| **Oral twang** | 0.7 | 0.3 | −0.2 | +0.4 | −1 | +1.5 | −0.3 | 0.95 | 0.0 | sealed VP port; theatre belt / squillo character |
| **Nasal twang** | 0.7 | 0.3 | −0.2 | +0.4 | −1 | +1.5 | −0.3 | 0.5 | ~0.5 | ~10 mm velar gap; Southern US / Cockney character |

## Implementation plan

Staged so each step is mergeable on its own; behaviour is unchanged
through stage 1.

### Stage 1 — Rename & block-split (no behaviour change)

- Rename `MCP` → `AES` everywhere (param enum, XML defaults, UI label,
  display name).
- Inside `calcCrossSections`, split the single `if (mcpFactor < 1.0
  || mcoFactor < 1.0) { ... }` block into named sub-blocks: `aesArea`,
  `aesEpiglottisAP`, `aesPiriform`, `mcoArea`, `mcoTongueRoot`,
  `mediolateralPinch`. Same effect, but each effect is now in its own
  scope ready for re-routing in stages 2–3.
- Update `ControlsPanel.cpp:146` header to "AES sphincter" and `MCO`
  label to "Hypopharyngeal narrowing (megaphone)" pending the signed
  rework. Note in the section that real twang ≠ MCO.
- 2D overlay (`VocalTract2DPanel.cpp:128–175`) reads `MCP` / `MCO` →
  rename to `AES` / `MCO`.
- Tests: existing vowel + held-tone regressions stay bit-exact.

### Stage 2 — Add `TT` (thyroid tilt)

- New param at index 21, 0..1, neutral 0. Extend `NUM_PARAMS` and the
  XML defaults. Add UI slider in a new "Larynx posture" section.
- Move the `aesEpiglottisAP` block out of `AES` and re-derive it from
  `TT`. Replace the uniform −x translation with a rotation of the upper
  EPIGLOTTIS ribs around the inferior anchor (and mirror onto
  `EPIGLOTTIS_TWOSIDE`).
- Optionally feed `TT` into the AES window length (shortens by up to
  ~5 mm) to capture CT-driven shortening.
- Tests: holding `TT = 0` yields stage-1 acoustics; `TT > 0` only moves
  the epiglottis surface (no area change apart from the geometry it
  causes).

### Stage 3 — Replace `MCO` with signed `PW`

- Change `MCO` range to [−1, +1], rename to `PW`, neutral 0.
- Switch the centerline area scaling and ML scaling to support both
  directions. Use an asymmetric Hann (centroid biased toward 3.0 cm
  end) so positive PW preferentially widens the hypopharynx.
- Drop the back-of-tongue push from this block — `TRX/TRY` owns the
  tongue root. PW's tongue-root *advancement* on the +ve side acts via
  a different mechanism: a soft target on TRX, not a direct vertex
  push.
- Magnitude: ±50% area at PW = ±1, calibrated against the
  Höglund/Lindblad/Sundberg 2024 numbers.
- Tests: PW = 0 reproduces stage-2 vowel acoustics; PW = +1 increases
  hypopharyngeal area without changing the AES window.

### Stage 4 — Soft clamps for PW > 0

- Clamp the widened UPPER_COVER ribs against `getPharynxBackX` so they
  don't pass through the back wall.
- Clamp the LOWER_COVER / tongue-root direction so positive PW doesn't
  push surfaces through the tongue body. Reuse the
  `restrictTongueParams` machinery (`VocalTract.cpp:4364`) where
  possible.
- Tests: at PW = +1 across the full vowel space, no surface penetrates
  another (visual + invariant).

### Stage 5 — Voice-quality presets in the Presets panel

Extend the existing `Tract Shapes` panel (`sources/live/PresetsPanel.cpp`,
`renderTractShapesPanel`) so it hosts voice-quality presets at the top
and the existing speaker / vowel / consonant grids below. The new
section reuses the same `segmentedButton` idiom already used by the
speaker switcher and the 3D-view solid/wire toggle.

**Layout (top → bottom):**

1. **Speaker** (existing, unchanged).
2. **Voice quality** (new section). One row of segmented buttons for
   the supraglottal style: `Neutral`, `Necessary twang`, `Distinct twang`,
   `Edge`, `Kulning`, `Squillo`, `Soprano formant-tune`, `SOVTE`. Below
   that, a smaller `Oral` / `Nasal` sub-row that's enabled only when a
   twang-family preset is selected.
3. **Vowels** (existing).
4. **Consonant articulator groups** (existing).

**Persistence rule (the key behavioural change):**

The voice-quality preset has two tiers of params it can write:

| Tier | Params | When written |
|---|---|---|
| **Core** (always) | `AES`, `TT`, `PW`, plus `VS` / `VO` when Oral / Nasal sub-preset is active | every voice-quality button click |
| **Reference posture** (opt-in) | `HY`, `JA`, `LD`, `TCY` (the advisory columns from the preset table) | only when the user has enabled the *Lock reference posture* toggle |

A single `[ ] Lock reference posture` checkbox sits next to the
voice-quality button row. Default off.

- The vowel / consonant preset writes everything *except* the params
  the active voice-quality preset currently owns — i.e. always skips
  the core set, and additionally skips the reference-posture set when
  the toggle is on. The existing `interp.to[p] = shapes[item.
  shapeIndex].param[p]` loop carries the current `from` value through
  for skipped params so the smoothstep is a no-op for them.
- Net result:
  - Toggle off (default): pick a voice quality once, walk through
    vowels / consonants normally without losing AES / TT / PW. Vowel
    fidelity is preserved.
  - Toggle on: voice-quality preset *also* sets the reference body
    posture for that style (e.g. larynx low + jaw open for Squillo).
    Vowels change only tongue / inner-mouth shape; the body posture
    stays locked. Useful pedagogically — "what should my body do for
    this style?"
- Picking `Neutral` clears the voice quality; the toggle persists.
- A small persistent state struct lives next to `ShapeInterpolation`:
  `{ activeVoiceQualityIndex, oralNasalMode, lockReferencePosture }`.
  Used to highlight the active button and to compute the per-frame
  skip-list.

**Why a global toggle and not per-param checkboxes.** A row of four
small checkboxes (`HY` `JA` `LD` `TCY`) would expose finer-grained
control but adds clutter and forces the user to remember which
columns are which. Per-param locks can be added later if it turns out
people want them; starting with a single toggle is the common-case
choice. The reference-posture set itself is a `static const` index
list in `PresetsPanel.cpp` and is trivial to extend (e.g. to add
`LP`).

**Voice-quality presets also write `snap.glottisParams`.** Per the
"Coupling between `TT` and the Glottis source" section above, the
source-side coloration that pairs with each style (e.g. lighter folds
for Squillo, firmer adduction for Belt) lives in the preset, not in
`TT`. Vowel/consonant clicks don't touch `glottisParams`, so this is
naturally sticky without any skip-list logic on the Glottis side.
Specific Glottis params written per preset come from the
implementation table in `PresetsPanel.cpp` (mainly
`RELATIVE_AMPLITUDE`, `LOWER_END_X`, `UPPER_END_X`, `PHASE_LAG`,
`PULSE_SHAPE`).

**Calibration targets from Fleischer 2022** (single Estill-certified
female subject, three pitches, two vowels — direct match to our
preset list). Acceptance test for the Stage 5 presets: with each
voice-quality preset selected on the JD2 speaker at A♭4 / /a:/, the
synthesised vocal tract should produce roughly:

| Preset | L (cm) | OP/HP ratio | Ic (EGG) | Notes |
|---|---:|---:|---:|---|
| Belting | 11.5 | 7.6 | ~0.5 | strong megaphone, 2fo-fR1 tuning |
| Twang | 12.2 | 6.8 | ~0.35 | megaphone, weaker than Belting |
| Speech (Neutral) | 12.0 | 4.4 | ~0.5 | mild megaphone, no formant tuning |
| Falsetto | 12.7 | 2.6 | ~0.2 | nearly neutral, abducted folds |
| Sobbing | 15.3 | 1.2 | ~0.5 | inverted-megaphone |
| Opera (Squillo) | 16.6 | 1.4 | ~0.5 | inverted-megaphone, fo-fR1 tuning |

These are guideline targets for tuning the per-preset deltas in
`PresetsPanel.cpp`, not strict acceptance gates — the synth's
geometry differs from the subject's anatomy, so absolute lengths
won't match exactly, but the *ordering* of preset lengths and OP/HP
ratios should hold. Validate qualitatively: Squillo should produce
the longest tract and lowest OP/HP, Belting the shortest and highest.

**Title:** rename the panel window from "Tract Shapes" to "Presets"
to reflect the broader scope. Keep the function name
`renderTractShapesPanel` — internal callsites don't need to churn.

**No XML schema change.** Preset data lives as a `static const` table
in `PresetsPanel.cpp` (or a new `VoiceQualityPresets.cpp` if it grows).

### Cross-cutting concerns (apply to multiple stages)

These don't fit neatly into one stage but need to be solved before
landing the stages they touch.

- **Speaker-file backward compatibility.** `data/speakers/JD2.speaker`,
  `W02.speaker`, `M01.speaker` each have `<param index="19" name="MCP"
  …/>` and `index="20" name="MCO"`. Tract-shape entries in the same
  files reference params *by name* (`<param name="HX" value="…"/>`),
  not by index. After Stage 1's rename, the param-block declarations
  must change, **and** the XML reader needs an alias map so old
  speaker files / tract shapes / gestural scores that refer to `MCP`
  / `MCO` continue to load correctly. Confirmed only the param block
  itself uses `MCP`/`MCO` in the bundled speaker files (vowel shapes
  don't, because they're 0), but third-party shapes might. Add the
  alias on load: `MCP → AES`, `MCO → PW` (with sign flip — see Stage
  3 below).
- **NUM_PARAMS bump for Stage 2.** Adding `TT` takes the count from
  21 to 22. Consumers (`AnatomyParams.cpp:934–1123`,
  `GesturalScore.h:166–168`, `Synthesizer.cpp:150,483`) loop over
  `NUM_PARAMS` and so are robust to the bump. Old gestural scores on
  disk will be one column short — load with `TT = 0` (= no tilt =
  current behaviour). Old speaker files lack the `<param index="21"
  name="TT" …/>` declaration — load with the built-in default range
  / neutral so the slider still works.
- **Stage 3 sign-flip on `MCO → PW` mapping.** Old `MCO ∈ [0, 1]`
  with `1` = full narrowing. New `PW ∈ [−1, +1]` with `−1` = full
  narrowing. Loader maps `PW := −MCO_old` when reading speaker files
  / tract shapes / gestural scores. Document the convention so it
  can't be forgotten.
- **`audio_regression_test.cpp` baseline preservation.** Each stage's
  acceptance criterion is "the regression test still produces
  bit-exact output". For Stage 1 that's automatic (rename only). For
  Stage 2/3, only when the new params are at neutral (`TT = 0`,
  `PW = 0`) — the regression input file must be confirmed not to
  exercise `MCP`/`MCO` (they're 0 in `data/example01.ges` per the
  default speakers). If the regression file needs updating, do it as
  a *separate* commit that explicitly renames the baseline.
- **`synthesis_bench` perf check.** After each behaviour-changing
  stage (2, 3, 4), re-run `synthesis_bench` and `wasm_bench`. Any
  regression > 5 % blocks the merge unless explained — the
  Synthesizer tract cache (E1) is the primary defender of held-tone
  cost; new effects must not invalidate the cache more often than
  before.
- **Engine tract-cache key.** The cache in `Synthesizer.cpp` is keyed
  on `tractParams[]`. Adding `TT` extends that vector, which is
  fine; sign-changing `PW` is also fine; just make sure no stage
  introduces an effect that depends on *non-param* state (frame
  counter, time, etc.) without invalidating the cache.
- **Glottis-shape combo staleness in Stage 5.** When a voice-quality
  preset writes `snap.glottisParams`, the existing Glottis-shape
  combo dropdown (`ControlsPanel.cpp:155–177`) will go to "(custom)".
  Expected, but worth a code comment so it's not flagged as a bug.

### Visualization & 2D control exposure

Both panels render the tract by walking `tract->surfaces[]`, so any
deformation that `calcCrossSections` applies to those surfaces shows
up automatically — *but only along the axes the panel actually
shows*. There's a real visualization gap and a UX gap, both worth
calling out.

**3D panel (`VocalTract3DPanel.cpp`).** Renders the closed `_TWOSIDE`
mesh variants (`UPPER_COVER_TWOSIDE`, `LOWER_COVER_TWOSIDE`,
`EPIGLOTTIS_TWOSIDE`, etc.) with no param-name references. As long as
each new deformation in `calcCrossSections` mirrors onto the
`_TWOSIDE` mesh (the existing AES code in `VocalTract.cpp:5741–5751`
and `5822–5831` is the template), 3D rendering needs no further
changes. Stage 2 (TT epiglottis tilt) and Stage 3 (PW > 0 widening)
must do that mirroring explicitly.

**2D panel mediosagittal slice (`drawOutline`,
`VocalTract2DPanel.cpp:194`).** Walks the same surfaces but only at
the midline (z ≈ 0). This means **z-axis (ML) deformations are
invisible in 2D**. Today's AES is partly visible because of the
−x epiglottis translation; the ML pinch contributes nothing to the
outline. For Stage 3, if positive `PW` only scales z (lateral
expansion), the 2D outline will not show the hypopharynx widening
even though 3D and acoustics will. Stage 3 must therefore include a
small **AP** component — push pharyngeal-back UPPER_COVER ribs in
+x for `PW > 0` (mirror in −x for `PW < 0`) — so the mediosagittal
view correlates with what the user is hearing. Same for Stage 2: TT
already deforms the epiglottis surface in the AP plane, so it's
visible by construction.

**2D overlay (`drawMedialCompressionOverlay`,
`VocalTract2DPanel.cpp:128–175`).** Highlights the AES and oropharynx
zones with warm-amber and cyan dots. After Stage 1 it just gets a
variable rename. After Stage 3 it needs to handle signed `PW`:
negative = cyan (current), positive = a distinguishable colour
(mint / green) so the user can read at a glance whether the pharynx
is being narrowed or widened. After Stage 2, an optional small
indicator at the thyroid-cartilage anchor showing the tilt angle is
nice but not required.

**2D control exposure.** Today the Articulation panel (`Controls
Panel.cpp`) has the only sliders for the new params. The 2D tract
panel exposes everything the user can drag *on the tract itself*
(tongue body, tongue tip, jaw, lips, lip protrusion) plus a
tongue-side-elevation inset (`computeTongueSideInset`,
`drawTongueSideInset`). The voice-quality params should follow the
same pattern: a small inset paired with the tongue-side one,
holding three vertical mini-sliders for `AES` (0..1), `TT` (0..1),
`PW` (−1..+1) with the baseline at 0 for `PW`. This is parallel to
how `TS3` (signed) is plotted today. Lower priority than the model
work but lands once `PW` exists, so it slots in around Stage 3.

In summary:

| Where | Stage 1 | Stage 2 | Stage 3 | Stage 5 |
|---|---|---|---|---|
| `calcCrossSections` AES block | rename + scope-split | move epiglottis-AP shift to `TT` | unchanged | — |
| `_TWOSIDE` mirror | unchanged | mirror new tilt rotation | mirror PW > 0 widening | — |
| 2D `drawOutline` | unchanged | auto-updates from tilt | needs AP push for `PW` to be visible | — |
| 2D `drawMedialCompressionOverlay` | rename vars | optional TT indicator | colour split for ±PW | — |
| 2D voice-quality inset (new) | n/a | n/a | new inset lands here | — |
| Glottis-shape combo | unchanged | unchanged | unchanged | comment about "(custom)" |

### Stage 3 — additional refinements

These follow naturally from the gap review and are folded into the
Stage 3 task description, not a separate stage:

- **Tongue body follows tongue root.** When `PW > 0` advances `TRX`,
  pushing only the root while the body stays put stretches the
  tongue surface unrealistically. Apply a fractional `TCX` advance
  (e.g. half the `TRX` delta) so the body trails the root. Verify
  the auto-tongue-root checkbox path in
  `ControlsPanel.cpp:124–138` still behaves correctly when `PW > 0`
  is interacting with `TRX`.
- **Auto-TRX interaction.** With "Auto (from tongue body)" on, `TRX`
  is computed from `TCX/TCY`. `PW > 0`'s tongue-root advancement
  should add a *target offset* on top of the auto value rather than
  overwrite it, so toggling auto on/off doesn't snap the tongue.
- **Reconsider Stage 4 separateness.** The PW > 0 soft clamps are
  arguably part of "make `PW > 0` not blow up the geometry" and
  could be folded into Stage 3. Keep it separate only if Stage 3
  ships first and clamping turns out to be substantial work.

### Stage 5 — additional refinements

- **`LP` belongs in the reference-posture set.** PDF p.13 names
  "lip retraction" alongside jaw drop and pharyngeal dilation as
  the soprano formant-tuning triad. Add `LP` to the reference-
  posture index list `{HY, JA, LD, TCY, LP}`.
- **`Neutral` preset semantics.** Clicking `Neutral` clears the
  voice-quality state: `AES = TT = PW = 0`, `VS / VO` go back to
  the underlying vowel's stored values, and (if Lock reference
  posture was on) `HY / JA / LD / TCY / LP` are released so the
  next vowel click reasserts the vowel's defaults. The Lock
  reference posture toggle itself stays as the user left it.
- **Hover tooltips.** Each voice-quality button gets a one-line
  tooltip (CVT / EVT terminology, the measured Aph/Ae from the
  literature where applicable) so the pedagogically curious can
  read what each preset is supposed to be doing without leaving
  the panel.
- **Glottis preset table calibration.** The per-preset Glottis
  param values (`RELATIVE_AMPLITUDE`, `LOWER_END_X`,
  `UPPER_END_X`, `PHASE_LAG`, `PULSE_SHAPE`) are educated
  estimates. Validate by perceptual A/B against reference
  recordings (Operatic squillo, theatre belt, Edge mode) before
  the panel ships; tune in the static table.

### Stage 6 — *Parked / future*

- **Velum & nasal pathway** — see "Background: velum & nasal coupling"
  above. After the Vampola 2020 re-evaluation, the minimum-viable
  list collapses to three items: velar tension `VT` (highest impact —
  controls whether sinus modes reach the lips at all), nostril
  radiation port, and lateral / asymmetric VP port. Sinus side-
  branches (maxillary, frontal, sphenoidal) and variable-area nasal
  cavity are dropped: with realistic wall damping their effect at
  the radiated lip output is ≤2 dB and just-audible-to-inaudible.
  None of this is required for stages 1–5 — the Oral / Nasal twang
  presets bind the existing `VS` / `VO` and that's enough for first
  cut.
- **Wall compliance / SOVTE.** Per-section dynamic state:
  ΔA_i ≈ C_w · A_i · P_supra(i), with C_w ≈ 6 × 10⁻⁵ cm³/dyn (Ohala &
  Riordan). Requires coupling supraglottal pressure back into the area
  function — non-trivial inside the TDS solver; design a separate
  experiment.
- **Source-filter coupling for Pth feedback.** Per Titze & Story
  1997, Pth drops sharply when Aₑ < ~1.0 cm² (parabolic curve, their
  Fig. 11 — flat above 1.0, steep below). Modelling this needs
  supraglottal inertive reactance fed back to the Glottis driving
  pressure. Out of scope for the area-function refactor — the only
  way to capture the +10–15 dB "free gain" of twang therapy in
  simulation, and the cleanest path to representing the 2fo–fR1 and
  fo–fR1 formant-tuning behaviours that Fleischer 2022 measured in
  Belting and Opera respectively.
- **Vallecula side-branch.** Side-branch off the centerline at the
  base of the tongue, between the tongue root and the epiglottis.
  Vampola 2015 and Fleischer 2022 both find that vallecular volume
  produces antiresonances around 4 kHz that *repel* F3–F5 — large
  vallecula (Sobbing, Opera) suppresses the singer's-formant
  cluster; small vallecula (Belting, Twang — tongue root retracts
  and epiglottis depresses, truncating the space) lets the cluster
  bunch up. Implementation pattern: same as the existing piriform-
  fossa side-branch in `getTube` (`VocalTract.cpp:7662–7683`) —
  Helmholtz / quarter-wave resonator with a volume_cm3 +
  ostium_area_cm2 anatomy parameter, length tied to the existing
  centerline geometry. The vallecular volume should shrink with
  tongue-root retraction (linkable to PW < 0 and to TRX, since
  Stage 3 already pushes the tongue root backward). Cheap relative
  to its acoustic effect — this is one of the few side-branches
  the literature consistently flags as audible at the lips.
- ~~**Pitch-aware AES factor.**~~ *Considered and rejected.* Yanagi
  observes 50% AES area reduction at low pitch vs 21% at high pitch,
  attributed to the cricothyroid stretch tensing the aryepiglottic
  tissue. The 21% figure is a population mean across professional
  singers in an MRI study, not a hard biomechanical ceiling — a
  trained singer pushing harder may well exceed it. Imposing the
  envelope automatically would tell the user "you can't" when in
  principle they often can, and the synth's job is to preserve
  capability, not to model typical singer behaviour. Presets that
  want to reflect operatic / soprano norms can dial AES down at
  high pitch explicitly; the slider itself stays full-range at any
  F0.

## References

### Twang biomechanics & acoustics

- Bae, Y., et al. (2020). *A pilot investigation of twang quality using
  magnetic resonance imaging.* Journal of Voice. PubMed:
  <https://pubmed.ncbi.nlm.nih.gov/32396757/>
- Yanagi et al. (2024). *Oropharyngeal and Aryepiglottic Narrowing for
  Twang: A Magnetic Resonance Imaging Study.* PubMed:
  <https://pubmed.ncbi.nlm.nih.gov/38964963/>
- Yanagi et al. (preprint). *The Vocal Tract in Loud Twang-Like Singing
  While Producing High and Low Pitches.* ResearchGate:
  <https://www.researchgate.net/publication/340669703>
- Leppävuori et al. (2020). *Characterizing Vocal Tract Dimensions in the
  Vocal Modes Using Magnetic Resonance Imaging.* PubMed:
  <https://pubmed.ncbi.nlm.nih.gov/32111459/> · author copy:
  <https://singandscream.fr/wp-content/uploads/2025/02/Leppavuori_JVoice_2020.pdf>
- Titze, I. R., & Story, B. H. (1997). *Acoustic interactions of the
  voice source with the lower vocal tract.* JASA 101(4):2234. The
  canonical reference for source-filter coupling: parabolic Pth vs
  Aₑ curve, 6:1 area-ratio target for singer's formant, narrow
  epilarynx + wide pharynx maintains positive inertive reactance
  below ~3 kHz, piriform-fossa pole-zero behaviour, no measurable
  effect of nasal coupling on Pth.
  <https://pubs.aip.org/asa/jasa/article-pdf/101/4/2234/8082279/2234_1_online.pdf>
- Fleischer, M., Rummel, S., Stritt, F., Fischer, J., Bock, M.,
  Echternach, M., Richter, B., & Traser, L. (2022). *Voice
  efficiency for different voice qualities combining experimentally
  derived sound signals and numerical modeling of the vocal tract.*
  Frontiers in Physiology 13:1081622. Single-subject MRI + FE +
  EGG study of all six Estill voice qualities (Speech, Falsetto,
  Sobbing, Oral Twang, Opera, Belting). Provides concrete L /
  OP/HP / Ic targets per voice quality used as Stage 5 calibration
  guidance, and corroborates Vampola's wall-damping value (μ ≈
  0.005). Open access:
  <https://www.frontiersin.org/articles/10.3389/fphys.2022.1081622/full>
- Titze, I. R. *The Acoustic Characteristics of Vocal Twang.* Utah Center
  for Vocology.
  <https://vocology.utah.edu/_resources/documents/the_acoustic_characteristics_of_vocal_twang_titze.pdf>
- Sundberg, J., & Thalén, M. *Source and filter adjustments affecting the
  perception of the vocal qualities twang and yawn.* ResearchGate:
  <https://www.researchgate.net/publication/5552831>
- Lombard, L. E., & Steinhauer, K. (2007). *A Novel Treatment for
  Hypophonic Voice: Twang Therapy.* ResearchGate:
  <https://www.researchgate.net/publication/7251780>
- Lombard, L. E., & Steinhauer, K. (2014). *The Use of the Twang
  Technique in Voice Therapy.* ASHA Journals:
  <https://pubs.asha.org/doi/10.1044/vvd24.3.119>
- Complete Vocal Institute. *Description and sound of Twang* (CVT
  framework, Necessary vs Distinct):
  <https://cvtresearch.com/description-of-twang/>
- UCLA SPL (2025 preprint). *Effects of false vocal fold adduction and
  aryepiglottic sphincter narrowing on the voice source in a
  three-dimensional…*
  <https://www.surgery.medsch.ucla.edu/spl/papers/2025JASA04_EpilaryngealConstriction.pdf>

### Pharyngeal widening, larynx height, formant tuning

- Mainka, A., et al. (2015). *Lower Vocal Tract Morphologic Adjustments
  Are Relevant for Voice Timbre in Singing.* PLOS One:
  <https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0132241>
- Echternach, M., et al. (2016). *Morphometric Differences of Vocal
  Tract Articulators in Different Loudness Conditions in Singing.* PLOS
  One:
  <https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0153792>
- Echternach, M., et al. (2009). *Vocal tract in female registers — a
  dynamic real-time MRI study.* PubMed:
  <https://pubmed.ncbi.nlm.nih.gov/19185452/>
- Echternach, M., et al. (2015). *Articulation and vocal tract acoustics
  at soprano subject's high fundamental frequencies.* PubMed:
  <https://pubmed.ncbi.nlm.nih.gov/25994691/>
- Höglund, A., Lindblad, P., & Sundberg, J. (2022/2024). *Three
  Professional Singers' Vocal Tract Dimensions in Operatic Singing,
  Kulning, and Edge — A Multiple Case Study Examining Loud Singing.*
  J. Voice. PubMed: <https://pubmed.ncbi.nlm.nih.gov/35277318/> · full
  text: <https://www.diva-portal.org/smash/get/diva2:1902994/FULLTEXT02>

### Aerodynamics, wall compliance, SOVTE

- Ohala, J. J., & Riordan, C. J. (1979). *Passive vocal tract enlargement
  during voiced stops.* UC Berkeley:
  <https://linguistics.berkeley.edu/~ohala/papers/ohala_and_riordan.pdf>
- Titze, I. R., & Story, B. H. *Vocalization with semi-occluded airways
  is favorable for optimizing sound production.* PMC:
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC8031921/> · PLOS Comput.
  Biol.:
  <https://journals.plos.org/ploscompbiol/article?id=10.1371/journal.pcbi.1008744>
- *Vocal tract adjustments to minimize vocal fold contact pressure
  during phonation.* PMC:
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC8425986/>

### Pharyngeal anatomy

- Statpearls — *Anatomy, Head and Neck, Stylopharyngeus Muscles.* NCBI:
  <https://www.ncbi.nlm.nih.gov/books/NBK547719/>
- *Pharynx — Anatomy, Neural Innervation, and Motor Pattern.* NCBI:
  <https://www.ncbi.nlm.nih.gov/books/NBK54279/>

### Velum, velopharyngeal port, nasal coupling

- Vampola, T., Horáček, J., Radolf, V., Švec, J. G., & Laukkanen, A.-M.
  (2020). *Influence of nasal cavities on voice quality: Computer
  simulations and experiments.* JASA 148(5):3218. The reference that
  drives the doc's minimum-viable nasal plan: realistic wall damping
  α ≈ 0.02 erases all internal nasal/sinus mode peaks from the
  radiated lip spectrum, leaving only F1 amplitude (–2.4 dB),
  frequency (+36 Hz), and bandwidth changes. PubMed:
  <https://pubmed.ncbi.nlm.nih.gov/33261400/> · PDF:
  <https://pubs.aip.org/asa/jasa/article-pdf/148/5/3218/15346495/3218_1_online.pdf>
- Havel, M., Kornes, T., Weitzberg, E., Lundberg J. O., & Sundberg,
  J. (2016). *Eliminating paranasal sinus resonance and its effects
  on acoustic properties of the nasal tract.* Logopedics Phoniatrics
  Vocology 41(1):33. Maxillary and sphenoid sinuses absorb rather
  than amplify under normal conditions — context for why dedicated
  side-branch modelling buys little at the radiated output.
- Pruthi, T., & Espy-Wilson, C. Y. (2007). *Acoustic parameters for the
  automatic detection of nasalization in vowels.* JASA / INTERSPEECH:
  <https://www.isca-archive.org/interspeech_2007/pruthi07b_interspeech.pdf>
- Chen, M. Y. (1997). *Acoustic correlates of English and French
  nasalized vowels.* JASA 102(4):2360.
  <https://pubs.aip.org/asa/jasa/article/102/4/2360/562330>
- Dang, J., Honda, K., & Suzuki, H. (1994). *Morphological and acoustical
  analysis of the nasal and the paranasal cavities.* JASA 96(4):2088.
  <https://pubs.aip.org/asa/jasa/article/96/4/2088/715181>
- Pruthi, T., Espy-Wilson, C. Y., & Story, B. H. (2007). *Simulation and
  analysis of nasalized vowels based on magnetic resonance imaging
  data.* JASA 121(6):3858.
  <https://pubs.aip.org/asa/jasa/article/121/6/3858/916403>
- Maeda, S. (1982). *The role of the sinus cavities in the production of
  nasal vowels.* ICASSP:
  <https://ieeexplore.ieee.org/document/1171506>
- Stevens, K. N. (1998). *Acoustic Phonetics.* MIT Press, ch. 9 (nasal
  consonants and nasalised vowels) — primary textbook reference for
  pole–zero structure of nasal coupling.
- Story, B. H. (2008). *Comparison of magnetic resonance imaging-based
  vocal tract area functions obtained from the same speaker in 1994 and
  2002.* JASA 123(1):327. PMC:
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC2377017/>
- Statpearls — *Anatomy, Head and Neck, Soft Palate.* NCBI:
  <https://www.ncbi.nlm.nih.gov/books/NBK542244/>
- Cleft Palate Foundation / Kummer A. *Velopharyngeal anatomy and
  physiology.* (Standard reference for velar muscle actions and closure
  patterns):
  <https://pubmed.ncbi.nlm.nih.gov/24084286/>
