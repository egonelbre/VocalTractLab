# Octagon-ring vocal tract — design sketch

Status: **design only, not implemented.** Lives on branch
`experiment/octagon-tract` so the doc can be iterated independently of
`main`.

## Motivation

Profiling (see `docs/optimization-plan.md`) shows that for the
gestural-score pipeline, **VocalTract::calculateAll dominates at 69%
inclusive**, and `getCrossProfiles` alone is 17% self / 58% inclusive.
Most of that cost is triangle-vs-plane intersection logic
(`Surface::appendTileTrianglesUnique`, `getEdgeIntersection`,
`getTriangleIntersection`, `classifyVertexLazy`) — work that exists
because the current pipeline goes:

```
articulators → 3D triangle meshes → cut by planes → 2D profiles → tube
```

For audio synthesis the only output that matters is the tube sequence:
`(length, area, articulator)` per section. The triangle meshes are
heavy machinery to derive a tiny output. The render path uses the
meshes directly, so they pay for themselves there — but the synthesis
path inherits the cost.

## Proposal

Replace the geometry pipeline for *synthesis* with a series of
**octagonal cross-section rings** anchored at centerline points.
Articulators are 3D deformation primitives; each ring's vertices are
displaced by the in-plane component of the primitive's
world-space displacement field at the vertex's position.

```
articulators (as 3D primitives) → deform octagon rings → tube
```

No triangle meshes, no plane intersections, no per-call dedup.

## Why octagons specifically

- **8 vertices is anatomically motivated.** The 4 cardinals correspond
  to the four physically meaningful boundaries: palate↑, tongue↓,
  cheeks ←→. The 4 diagonals capture corner curvature and avoid the
  staircase artifacts a 4-gon would show.
- **8-fold rotational symmetry under 45°** makes rest shapes trivial
  to define for the four axis-aligned anatomical structures.
- **Closed convex topology** — area is closed-form (shoelace), centroid
  is closed-form, ring-to-ring lofting produces a uniform 8-quad mesh.
- **Fixed memory layout** — every cross-section is `Point2D[8]`. No
  variable-length triangle lists, no `intersectionsPrepared[]` flags,
  no `triangleStamp[]` dedup arrays.
- **Generalizes** — if 8 isn't enough at sharp contact points (alveolar
  rings during plosive closures), the same code generalizes to 12 or
  16 vertices for selected rings.

## Data model

```cpp
struct OctagonRing {
  Point3D center;          // world-space center on the centerline
  Point3D u, v, n;         // orthonormal frame; n is centerline tangent
  Point2D vertex[8];       // local (u,v) coordinates of the 8 vertices
  Region  region;          // GLOTTAL / PHARYNGEAL / VELAR / PALATAL /
                           //   ALVEOLAR / LABIAL — selects rest shape
};

struct ParametricTract {
  static constexpr int NUM_RINGS = 64;  // matches Tube::NUM_SECTIONS

  OctagonRing ring[NUM_RINGS];

  // Replaces VocalTract::calculateAll() + getCrossProfiles().
  void buildFromArticulators(const ArticulatorState &a);

  // Reduces to the (length, area) tube sequence the TDS already
  // consumes. Same shape as VocalTract::getTube() output.
  void getTube(Tube &out) const;
};
```

## Deformation

Each articulator is a 3D deformation primitive (ellipsoid push, point
source with falloff, slab compression, …) exposing
`Point3D displacementAt(Point3D worldPos)`. Applying it to a ring:

```cpp
inline void deformRing(OctagonRing &r, const Articulator &a) {
  for (int k = 0; k < 8; k++) {
    const Point3D w = r.center + r.vertex[k].x * r.u + r.vertex[k].y * r.v;
    const Point3D d = a.displacementAt(w);
    // Project to local plane: in-plane components only. The normal
    // component would be a centerline shift (handled separately) or
    // redundant if the centerline is fixed.
    r.vertex[k].x += dot(d, r.u);
    r.vertex[k].y += dot(d, r.v);
  }
}
```

The "transform the deformation to the plane of the octagon" step is the
two `dot(d, r.u)` / `dot(d, r.v)` projections — the deformation is
authored in world space (where the articulator naturally lives), and
each ring receives only its in-plane component.

### Why drop the normal component

Picture the tongue body pushing forward (toward the lips). At a fixed
centerline position, the tongue tissue extends further forward — at
that *fixed plane*, the cross-section shape changes (less airway
where the tissue now reaches). That change is captured entirely by
the in-plane displacement; the normal-direction component would
correspond to "the cross-section moved along the airway", which is
either a centerline shift or, with fixed centerline, redundant.

If we eventually want centerline anchors that are themselves driven
by articulators, the normal component feeds into that — but it lives
in a separate update pass, not in the per-ring deformation.

## Articulator primitives

Each existing VTL articulator becomes one of a small catalog of 3D
primitives:

```cpp
struct EllipsoidPush : Articulator {
  Point3D center;          // driven by TX/TY (tongue body position)
  double rx, ry, rz;       // driven by TBX/TBY (body deformation)
  double stiffness;
  int firstRing, lastRing; // bounding-ring range for prefilter

  Point3D displacementAt(Point3D w) const override {
    Point3D r = w - center;
    double d2 = (r.x*r.x)/(rx*rx) + (r.y*r.y)/(ry*ry) + (r.z*r.z)/(rz*rz);
    if (d2 >= 1.0) return {0,0,0};
    double mag = stiffness * (1.0 - d2);
    return r * (mag / std::sqrt(dot(r,r) + 1e-9));
  }
};
```

Other useful primitives: point-source with Gaussian falloff (tongue
tip), slab compressor (jaw closure), rotation around a hinge (jaw
angle), ring-radius scale (lip rounding/spreading), elongation along
centerline (larynx height).

The catalog is finite — probably 10-15 distinct primitive types covers
all of VTL's existing articulators. Each is a few dozen lines of
arithmetic.

## Tube reduction

```cpp
void getTube(Tube &out) const {
  for (int i = 0; i < NUM_RINGS - 1; i++) {
    out.section[i].length_cm = (ring[i+1].center - ring[i].center).length();
    // Average the two endpoint octagon areas — same as the trapezoidal
    // rule on a tube section.
    const double aHere = octagonArea(ring[i].vertex);
    const double aNext = octagonArea(ring[i+1].vertex);
    out.section[i].area_cm2 = 0.5 * (aHere + aNext);
    out.section[i].articulator = dominantArticulator(ring[i]);
  }
}

inline double octagonArea(const Point2D v[8]) {
  double a = 0.0;
  for (int k = 0; k < 8; k++) {
    int j = (k + 1) & 7;
    a += v[k].x * v[j].y - v[j].x * v[k].y;
  }
  return 0.5 * std::fabs(a);
}
```

`dominantArticulator` returns whichever articulator most strongly
deformed the ring — feeds the noise-source classification logic in
`TdsModel::calcNoiseSources`.

## Cost estimate

Per `buildFromArticulators` call:

- 64 rings × 8 vertices × ~10 active articulators
- Per (ring, vertex, articulator): ~20 flops (ellipsoid test +
  projection + add)
- Plus centerline layout: 64 anchors × O(few) flops each

Rough total: **< 50 K flops, on the order of microseconds.** Compared
to the current ~1.8 ms `BM_TractToTube`, that's a ~1000× synthesis-side
speedup. The acoustic solver becomes the bottleneck again, which is
where it should be for a synthesis backend.

## Mesh / renderer hand-off

The triangle pipeline currently serves two masters: synthesis and the
GUI's `VocalTractPicture`. Once synthesis is on octagons, the GUI has
two options:

1. **Keep the existing triangle anatomy as a renderer-only asset.** No
   regression risk for the GUI. Triangle meshes are no longer in the
   audio hot path.

2. **Render the octagon rings as a tube mesh.** Adjacent rings connect
   with a uniform 8-quad topology — natural to render, automatic
   continuity. This becomes the long-term single source of truth.

(1) is the safe interim; (2) is the cleanup, after parametric matches
the current model's anatomy quality.

## Migration plan

1. **Ship `ParametricTract` alongside `VocalTract`** behind a runtime
   option. Both produce `Tube` for the acoustic solver, so nothing
   downstream changes.

2. **Author one region first** — palatal/oral is the cleanest starting
   point. Fit primitives by sweeping articulator settings and matching
   the existing `getTube()` output (same hash-based regression
   technique used in `tests/audio_regression_test.cpp`). The
   `audio_regression_reference.raw` machinery quantifies drift exactly
   as the existing optimization PRs do.

3. **Cross-validate on `BM_TractToTube`** — once the parametric path
   reproduces the triangle path's output to acceptable drift (target:
   `max|d| < 1e-10` on a representative articulator sweep), measure
   the wall-time speedup on the benchmark.

4. **Region-by-region rollout.** Add pharyngeal, then velar, then
   alveolar, then labial, then glottal. Each region has its own audio-
   regression hash; PRs are tractable in size.

5. **Default to parametric** once all regions match. Triangles stay as
   the renderer's input until (or unless) the GUI is ported.

## Open questions

These need to be answered before — or as part of — the prototype.

- **Vertex count per ring.** Is 8 always enough, or do alveolar rings
  during plosive closures need 12/16? Easy to answer with a fitter:
  measure RMS contour error vs vertex count on a representative
  articulator grid.

- **Centerline anchors vs ring deformation.** Which articulator effects
  modify the centerline (jaw angle, larynx height) vs which deform
  rings around a fixed centerline (tongue body, lip rounding)?
  Probably a mix: we need both pathways and a clear rule for which
  goes where.

- **Contact handling.** When the tongue tip presses against the
  alveolar ridge, the airway closes. With a fixed-vertex octagon, the
  closure is "small area" not "explicit contact". The current model
  has the same issue (small area, not zero). May not matter — the
  acoustic solver clamps area to `MIN_AREA_CM2` anyway — but worth
  confirming during the prototype.

- **Asymmetric configurations.** Lateral /l/, tongue grooving for /s/
  — these should fall out from non-symmetric articulator primitives.
  Verify in the prototype that they do.

- **Ground-truth fitting.** Use the existing triangle pipeline's
  output as ground truth, or refit against MRI data? First option is
  faster and decouples this work from anatomy research; second option
  is cleaner long-term. Probably (a) for the prototype, (b) is a
  later cleanup.

## What this replaces

If this lands fully:

- `sources/vtl/anatomy/Surface.{h,cpp}` — gone for synthesis. May stay
  as a renderer asset under (1) above.
- `VocalTract::getCrossProfiles`, `calcCrossSections`,
  `calcSurfaces`, `appendTileTrianglesUnique`,
  `getTriangleIntersection`, `getEdgeIntersection`,
  `classifyVertexLazy`, `prepareIntersection(s)`,
  `setupProfileLine`, the upper/lower profile post-processing
  passes — all gone.
- `intersectionsPrepared[]`, `triangleStamp[]`,
  `vertexTested[]`, `vertexSide[]`, `edgeTested[]`,
  per-tile triangle lists — all gone.
- The whole BM_TractToTube hot path collapses to a few function calls.

The synthesis bench's `BM_GesturalScoreToAudio` should drop from
~1.3 s to wherever the acoustic solver's intrinsic cost lands —
roughly half, possibly more, depending on how much of the residual
1.3 s is geometry vs solver.
