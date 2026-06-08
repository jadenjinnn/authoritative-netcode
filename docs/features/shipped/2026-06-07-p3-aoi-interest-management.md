# Feature spec: P3 slice 3 — AOI / interest management

> Tier: **Design**
> Status: **shipped**
> Started: 2026-06-07
> Spec author: Jaden Jin (drafted by Claude, pending approval)

---

## Classification

- **Reversibility**: Reversible
- **Scope**: Cross-cutting
- **Tier rationale (1 sentence)**: Reversible (AOI is additive filtering; co-deployed, no external consumers) but cross-cutting once the density experiment (2-full) makes the world bound + quantize range configurable — touching `World`, `quantize`, the codec, server, and bots — which lands it on Design; the moving-AOI delta also has correctness edge-cases worth pinning first.

---

## Problem

State sync still sends every client every entity (after slices 1-2, just smaller and delta'd). AOI sends each client only entities within radius R of its own position, so each client's data is governed by **local neighbor count k**, not total population N. In a **fixed** world that's a large constant-factor cut (~AOI-area/world-area); the asymptotic O(N·k) win only appears under **bounded density** (the world grows with population). This slice does both: implement AOI, measure the constant-factor cut in the fixed world, and — by scaling the world with N at constant density — demonstrate per-client egress going **flat** across N, the real curve-bend.

## In scope

**Phase A — core AOI (fixed world):**
- Per-client AOI filter inside `SnapshotHistory::encode_for`: send only entities within radius R of the viewer's own entity; the viewer always includes itself.
- Reuse the slice-1 delta: filter **both** the current snapshot and the baseline snapshot (each centered on the viewer's position *at that tick*) and run the existing `encode_delta` — entered-AOI = add, left-AOI = remove, stayed = normal delta.
- Brute-force spatial query (O(N) per client). Circle test (`dx²+dy² ≤ R²`), R = 20.
- Metric: average entities-in-AOI (k).
- Measure fixed-world (100×100) egress vs slice-2 (the constant-factor cut).

**Phase B — density-controlled sweep (2-full):**
- Make the world bound configurable (`World(bound)`) and the quantize range track it (`quantize(v, max)` / `dequantize(q, max)`; `kPosBits` stays 12 so per-entity **bytes** stay constant across world sizes).
- Uniform-random seeded spawn across `[0, bound]²` (so density is actually uniform during the measurement window, not a slow center-spawn diffusion).
- Server + bots take the world bound as a launch arg (both must match).
- Sweep N at constant density (e.g. N=50→bound 100, N=100→141, N=200→200; R=20 fixed → k≈6 constant) and show per-client egress ~flat across N.

## Out of scope

- Grid / spatial index — brute force now; grid is a measured P6 optimization.
- Self-describing world bound on the wire — both processes are launched with matching bounds; a wire field is future work.
- Variable/per-client R, hysteresis on AOI entry/exit, occlusion/PVS by geometry — radius only.
- Sending the viewer extra "awareness" beyond its radius (teammates, last-known positions of left entities).
- Scaling entity *speed* with world size — keep `kSpeed`; uniform spawn removes the need.
- Any change to slice-1/2 wire structure beyond threading the quantize range.

## API sketch

```cpp
// quantize.h — range is now a parameter (kPosBits stays constexpr 12)
constexpr int kPosBits = 12;
uint32_t quantize(float v, float max);      // maps [0, max] -> [0, 2^kPosBits - 1]
float    dequantize(uint32_t q, float max);

// snapshot codec — gains the world max so it can (de)quantize
std::vector<uint8_t> encode_keyframe(const WorldSnapshot& snap, float world_max);
std::vector<uint8_t> encode_delta(const WorldSnapshot& curr, const WorldSnapshot& baseline, float world_max);
bool apply_snapshot(WorldSnapshot& base, const uint8_t* data, size_t len, float world_max);

// SnapshotHistory — knows the world bound; encode_for gains the viewer + radius
class SnapshotHistory {
public:
    explicit SnapshotHistory(size_t depth = 128, float world_max = 100.0f);
    // AOI-filtered keyframe/delta for one viewer (centered on the viewer's own
    // position at each tick). Keyframe if baseline is 0 / aged out / viewer absent there.
    std::vector<uint8_t> encode_for(EntityId viewer, float radius,
                                    uint32_t baseline_tick, uint32_t current_tick) const;
};

// World — configurable bound + uniform seeded spawn
class World {
public:
    explicit World(float bound = 100.0f, uint32_t seed = 1u);
    EntityId add_player();   // spawns uniformly at random in [0, bound]^2
};

// Server / bots take the bound as a launch arg; both must match.
// server: SnapshotHistory(128, bound); encode_for(cs.entity, kAoiRadius, cs.last_received_tick, tick)
// bots:   apply_snapshot(baseline, payload, len, bound)
```

---

## Design

### Data structures

- `SnapshotHistory` stores `world_max_`. `encode_for(viewer, radius, baseline_tick, current_tick)`:
  1. `curr = get(current_tick)`; if absent → `{}`.
  2. center_now = viewer's entity in `curr`; if absent → `{}` (server guarantees the viewer exists).
  3. `current_view = {current_tick, filter(curr, center_now, radius)}`.
  4. if `baseline_tick != 0` and `get(baseline_tick)` exists and viewer is in it → `baseline_view = {baseline_tick, filter(base, center_then, radius)}`, return `encode_delta(current_view, baseline_view, world_max_)`.
  5. else → `encode_keyframe(current_view, world_max_)`.
- `filter` keeps entities with `(x-cx)² + (y-cy)² ≤ radius²` (viewer included, distance 0). Entity lists stay id-sorted (filtering preserves order), so `encode_delta`'s two-pointer diff still holds.
- `World` gains `float bound_` and a seeded `std::mt19937` for uniform `[0,bound]²` spawn.
- `constexpr float kAoiRadius = 20.0f;` (server-side; passed into `encode_for`).

### Why this stays correct (the moving-AOI delta)

The client's confirmed baseline *is* `filter(history[baseline_tick], viewer-pos-then, R)` — by induction: a keyframe sets the client's set to exactly that, and each delta is computed as `current_view` vs the client's confirmed `baseline_view`, so applying it lands the client on `filter(history[T])`. Recomputing `baseline_view` server-side is the same deterministic function (same R, same center from history) → it matches what the client holds. Entered/left fall out of the existing add/remove diff.

### Module touchpoints

- `sim/quantize.{h,cpp}` — `kPosMax` constant → `max` parameter on `quantize`/`dequantize`.
- `sim/snapshot.{h,cpp}` — `encode_keyframe`/`encode_delta`/`apply_snapshot` gain `world_max`; internals pass it to (de)quantize.
- `sim/snapshot_history.{h,cpp}` — ctor stores `world_max`; `encode_for` gains `viewer`+`radius`, does the AOI filter; new `filter`/find-viewer helpers (anon namespace).
- `sim/world.{h,cpp}` — `World(bound, seed)`; uniform-random spawn; clamp to `[0,bound]`.
- `server/main.cpp` — world bound launch arg; `World(bound)`, `SnapshotHistory(128,bound)`; `encode_for(cs.entity, kAoiRadius, …)`; avg-AOI gauge.
- `bots/main.cpp` — world bound launch arg; `apply_snapshot(…, bound)`.
- Tests — update all `encode_*`/`apply_snapshot`/`encode_for` call sites for new signatures; add AOI tests.

### Lifecycle / control flow

Per tick the server already pushes the full snapshot to history and loops clients. Only the encode call changes: `history.encode_for(cs.entity, kAoiRadius, cs.last_received_tick, tick)`. Filtering happens on full-precision server-side positions (the client never computes AOI). The bot is passive: it receives a (smaller) filtered keyframe/delta and applies it exactly as before, passing its launch `bound` to `apply_snapshot`.

### Edge cases

- **Viewer always sees itself** (distance 0 ≤ R) — guarantees a non-empty view and a stable center.
- **Viewer absent in baseline snapshot** (confirmed a tick before it spawned — shouldn't happen) → keyframe re-seed.
- **Baseline aged out / none** → keyframe (unchanged behavior).
- **Entity exactly at distance R** → inclusive (`≤`), consistently on both filters.
- **world bound mismatch server↔bots** → wrong dequantize (garbage positions); launch invariant, documented.
- **Clustered start** (without uniform spawn) → everyone in everyone's AOI → no win until spread; Phase B's uniform spawn removes this for the measurement.
- **Quantize precision vs world size** — bigger world at fixed `kPosBits` = coarser step, but constant bytes (the point); documented.

---

## Why this approach

- **Filter both snapshots + reuse delta (A1)**: an entity entering/leaving AOI is exactly an add/remove, which the slice-1 delta already encodes. Centering the baseline filter on the viewer's position *at the baseline tick* makes the recomputed baseline match what the client holds, so no per-client state is stored. Rejected: storing each client's last-sent set (A2 — extra state/bookkeeping for an unmeasured CPU saving); AOI keyframes only (A3 — discards delta).
- **Circle, R=20, brute force**: circle = radial awareness (render-distance-like); R is the headline dial; brute force is naive-first (the win is egress, not CPU — grid deferred to P6 with a before/after number).
- **2-full density sweep**: a fixed world makes AOI a constant-factor cut that *still* grows O(N²) as density rises — so to honestly show the O(N·k) curve-bend, density must be held constant by scaling the world with N. `kPosBits` stays fixed so per-entity bytes are constant across world sizes (clean bandwidth comparison); precision degrades, which is acceptable and noted. Rejected: fixed-world + shrinking R∝1/√N (gets a flat line but via an artificial view-distance policy — muddies the story); 2-lite egress-vs-k (rigorous but not the flat-vs-N graph asked for).
- **Tradeoffs accepted**: brute-force O(N²) query CPU; world-bound launch invariant (no self-describing wire field yet); coarser quantization in larger worlds.

## Risks / what could go wrong

- **Moving-AOI delta desync (silent).** If the baseline filter is centered on the *current* position instead of the position *at the baseline tick*, entered/left sets are wrong and the client drifts (ghost entities that left, or missing entities that entered) with no error. Guard: tests that move an entity across the radius and move the *viewer* across ticks, asserting the decoded set equals `filter(current)`.
- **world bound mismatch server↔bots** → all positions dequantize wrong; bots "see" garbage. Launch-time invariant; a wire field would remove it (future work). Guard: a test that round-trips with mismatched `world_max` and shows it diverges (documents the contract).
- **Non-uniform density invalidates the sweep.** Center-spawn diffusion means density isn't constant during the window; the "flat" line would be an artifact. Mitigation: uniform seeded spawn (Phase B), and report measured avg-k per run to confirm it's actually constant.
- **Brute-force CPU at the sweep's top end.** O(N²) filtering at N=200 in a bigger world — watch `netcode_tick_rate_hz`; a sag is a documented finding (and the motivation for the P6 grid), not a blocker.
- **Signature churn breaks many call sites.** Threading `world_max` + the `encode_for` args touches every test; mechanical but easy to miss one. Build + full ctest gate each phase.

## Success criteria

**Phase A (core AOI):**
- All tests green incl. new AOI cases:
  - **Filters to radius:** a keyframe for a viewer contains only entities within R (+ the viewer).
  - **Viewer always present**, even alone / at a corner.
  - **Entered/left across a delta:** an entity crossing into the radius appears as an add; out of it, a remove; after `apply_snapshot` the client's set equals `filter(current)`.
  - **Moving viewer:** the center shifts between ticks; entered/left computed against the *moving* center.
- Fixed-world (100×100) egress drops markedly vs slice-2 at N=50/100 (the constant-factor cut), 60 Hz held; avg-k reported.

**Phase B (density sweep):**
- At constant density (N=50/100/200 with bound 100/141/200, R=20), **per-client egress is ~flat across N** (within noise) while slice-2's per-client cost roughly doubled — the O(N·k) signature. Measured avg-k confirmed ~constant across the runs. Tick rate recorded.

## Test extensions required

- New `tests/aoi` cases (in `snapshot_history_test.cpp` or a new `aoi_test.cpp`): filters-to-radius, viewer-included, entered/left-across-delta, moving-viewer.
- Update every `encode_keyframe`/`encode_delta`/`apply_snapshot` call site in `snapshot_delta_test.cpp` and `world_test.cpp` for the `world_max` arg; update `snapshot_history_test.cpp` `encode_for` calls for `viewer`+`radius` (existing keyframe/delta-selection tests pass a present viewer + huge radius = no filtering).
- A quantize test that `quantize(v,max)`/`dequantize(q,max)` round-trips across a couple of `max` values (e.g. 100 and 200).
- No new automated integration test for the sweep itself (it's a measurement run); the AOI correctness is covered by the unit cases above.

---

## Decisions during implementation

<!-- Append-only; dated; non-obvious choices made while building. -->

### 2026-06-07 — random spawn rippled into a World test; world_max threaded everywhere
- Uniform-random spawn (Phase B) broke `World.ApplyInputMovesOnlyTargetedEntity`, which used
  `a.x > b.x` as a proxy for "a moved" (valid only when both spawned at the same point). Fixed to
  assert `a.x > a's own spawn x` and `b unchanged` — the real intent, regardless of spawn position.
- `quantize`/`dequantize` gained a `max` (world bound) parameter, threaded through
  `encode_keyframe`/`encode_delta`/`apply_snapshot` and stored on `SnapshotHistory`; server and bots
  take the bound as a launch arg and MUST match (a mismatch silently mis-dequantizes positions).
  Self-describing the bound on the wire is parked in Future work.

## Spec amendments

<!-- Append-only; dated; times the spec was wrong mid-build. -->

---

## Future work (out-of-scope ideas surfaced during this feature)

- Grid / spatial hash for the AOI query (P6 profiling, with a before/after number).
- Self-describing world bound (and quantize params) on the wire, so client/server can't silently disagree.
- AOI hysteresis (don't flap entities right at the radius edge) and "last-known position" for entities that just left.
- Per-client / role-based interest (spectators, teammates beyond radius).
