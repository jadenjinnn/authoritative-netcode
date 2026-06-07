# Feature spec: P3 slice 1 — delta compression for state sync

> Tier: **Design**
> Status: **shipped**
> Started: 2026-06-06
> Spec author: Jaden Jin (drafted by Claude, pending approval)

---

## Classification

- **Reversibility**: Reversible
- **Scope**: Cross-cutting
- **Tier rationale (1 sentence)**: Reversible (client+server co-deployed, no external consumers, full-state path kept as the keyframe fallback) × cross-cutting (codec, snapshot history, input format, server loop, bot client, metrics) lands on Design; it brushes the wire format but reversal is cheap, so it does not tip to Full spec.

---

## Problem

The server broadcasts the full world state to every client every tick (`server/main.cpp:140`) — O(entities × clients), the P2 baseline (~7 MB/s at N=100). Delta compression is P3's first bandwidth lever: send only what changed since the snapshot the client last confirmed. On the all-moving bot workload the *steady-state* bandwidth win is ~0 by construction (every entity's `{x,y}` changes every tick), so the deliverable is two-fold: (1) the baseline / ack / snapshot-history machinery that P4 reconciliation and the later AOI slice both reuse, and (2) an honest measured number showing delta ≈ full-state at 100% churn — the result that motivates the next slices (quantization, AOI).

## In scope

- Tagged snapshot wire format: a 1-byte type discriminating **keyframe** (full state) from **delta** (relative to a baseline tick).
- Entity-level delta: changed + newly-added entities sent in full; unchanged entities omitted (absence = "carry forward"); removed entity ids listed explicitly.
- App-level snapshot ack: the client→server input packet carries `last_received_tick`; the server records each client's confirmed baseline.
- Server-side `SnapshotHistory` ring of recent full snapshots to diff against; per-client delta encode.
- Full-state keyframe fallback when a client has no baseline (just joined, tick 0) or its baseline has aged out of the ring.
- Bot client (`bots/main.cpp`): decode inbound snapshots, hold a baseline, apply deltas, stamp `last_received_tick` on outgoing inputs — without this the server only ever sends keyframes and the measurement is meaningless.
- Metrics: `netcode_keyframe_packets_total` and `netcode_delta_packets_total` so the dashboard shows the delta share (proving deltas are actually flowing).

## Out of scope

- Bit packing / quantization — the next P3 slice (the reserved hand-write: BitWriter/BitReader + quantization).
- AOI / interest management — a later P3 slice; this is where the real O(N²) win comes from.
- Field-level delta / per-entity changed-field bitmasks (buys nothing on `{x,y}`-both-change; deferred to the bit-packing slice).
- Idle/static "scenery" entities — we chose to own the negative result; the idle-entity knob is in Future work.
- Reliable snapshot delivery or forcing keyframes on detected loss beyond the age-out fallback.
- Disconnect detection / entity removal plumbing on the server (the removed-id path is in the format for correctness, but nothing triggers it today).
- Client→server (input) compression — the cheap direction; not a bottleneck.
- The P0 relic client (`client/main.cpp`) — raw-UDP, legacy `Snapshot` struct, already off-protocol; untouched.
- Reconciliation / prediction (P4).

## API sketch

C++ only (no Python in this project).

```cpp
// --- Wire formats (server -> client), little-endian, type-tagged ---
// keyframe: [u8 type=0][u32 tick][u16 count]{ [u32 id][f32 x][f32 y] }*
// delta:    [u8 type=1][u32 tick][u32 baseline_tick]
//           [u16 changed_count]{ [u32 id][f32 x][f32 y] }*   // changed + added
//           [u16 removed_count]{ [u32 id] }*

// --- Input (client -> server) gains the confirmed baseline ---
// [u32 last_received_tick][f32 dx][f32 dy]      // last_received_tick == 0 => "no baseline yet"

// --- Server: keep a rolling history, ask it for a per-client blob ---
sim::SnapshotHistory history;                    // ring of recent WorldSnapshots
history.push(tick, world.snapshot());
std::vector<uint8_t> blob = history.encode_for(client.last_received_tick, tick);
//   -> keyframe if baseline == 0 or absent from the ring; else delta(baseline -> tick)

// --- Client (bot): decode tag, patch or replace, then ack ---
sim::WorldSnapshot& base = current_;             // last good full state held by this client
if (sim::apply_snapshot(base, data, len)) {      // keyframe replaces; delta patches iff baseline matches
    last_received_tick = base.tick;              // stamped into the next outgoing Input
}
```

---

## Design

### Data structures

- `enum class SnapshotType : uint8_t { Keyframe = 0, Delta = 1 };` — the leading wire byte (`sim/snapshot.h`).
- `sim::SnapshotHistory` (new, `sim/snapshot_history.{h,cpp}`): fixed-depth ring of `{uint32_t tick, WorldSnapshot}`.
  - depth ≈ 128 ticks (~2.1 s @ 60 Hz) — must comfortably exceed worst-case ack latency (RTT + jitter + a few dropped acks) under our sim settings.
  - `void push(uint32_t tick, WorldSnapshot snap);`
  - `const WorldSnapshot* get(uint32_t tick) const;` — `nullptr` if aged out / never present.
  - `std::vector<uint8_t> encode_for(uint32_t baseline_tick, uint32_t current_tick) const;` — keyframe when `baseline_tick == 0` or `get(baseline_tick) == nullptr`, else delta of `get(current_tick)` against `get(baseline_tick)`.
- Codec in `sim/snapshot.{h,cpp}`, alongside (replacing) the current full-state pair:
  - `std::vector<uint8_t> encode_keyframe(const WorldSnapshot&);` (the existing `encode_snapshot` body + a type tag).
  - `std::vector<uint8_t> encode_delta(const WorldSnapshot& curr, const WorldSnapshot& baseline);`
  - `bool apply_snapshot(WorldSnapshot& base, const uint8_t* data, size_t len);` — replaces `base` on a keyframe; on a delta, applies iff `base.tick == wire.baseline_tick` (else returns false, leaves `base` untouched). Both `WorldSnapshot` entity lists stay sorted by id so the diff/merge is a linear two-pointer walk.
- `sim::Input` gains `uint32_t last_received_tick = 0;`. `encode_input`/`decode_input` switch from raw struct `memcpy` to explicit field encode (the raw memcpy is layout-fragile once a field is added, and we want an intentional wire order).
- Server per-client state: replace the bare `std::map<Endpoint, EntityId> entity_of` with `struct ClientState { sim::EntityId entity; uint32_t last_received_tick = 0; };` keyed by endpoint.

### Module touchpoints

- `sim/snapshot.h/.cpp` — `SnapshotType` tag; `encode_keyframe` (from existing `encode_snapshot`), `encode_delta`, `apply_snapshot`; keep the legacy single-entity `Snapshot` struct for the relic.
- `sim/snapshot_history.h/.cpp` — new `SnapshotHistory` ring (CMake target gets the new source).
- `sim/input.h` — add `last_received_tick`; explicit encode/decode.
- `server/main.cpp` — own a `SnapshotHistory`; read each client's `last_received_tick` from inbound inputs (monotonic max); per-client `encode_for` instead of one shared blob; increment keyframe/delta counters.
- `bots/main.cpp` — decode inbound snapshots into a per-bot baseline via `apply_snapshot`; stamp `last_received_tick` into each outgoing `encode_input`.
- `server/main.cpp` metrics block — two new counters.
- CMake — add `snapshot_history` to the sim library and a new test target.

### Lifecycle / control flow

Server, per tick:
1. `poll()` inbound; per client, decode inputs and set `last_received_tick = max(current, decoded)` (never regress — guards stale/reordered acks).
2. `step()` world, `++tick`.
3. `history.push(tick, world.snapshot())`.
4. For each client: `blob = history.encode_for(client.last_received_tick, tick)`; `queue_unreliable(blob)`; bump keyframe or delta counter.
5. `update_all()`.

Bot client, per received snapshot:
1. Read the type tag.
2. Keyframe → `apply_snapshot` replaces the held baseline. Delta → applies iff held `base.tick == baseline_tick`; otherwise drop (stale/reordered) and keep the old baseline.
3. On success, `last_received_tick = base.tick`, carried in the next outgoing input.

The loop ratchets: client holds B, acks B; server sends delta(B→T); client advances to T, acks T; next delta is (T→T'). A lost delta leaves the client at B still acking B, so the next delta(B→newer) still applies — self-healing.

### Edge cases

- **New client**: `last_received_tick == 0` → keyframe until the first ack > 0 arrives (a few keyframe ticks at join — fine).
- **Baseline aged out** (stall / heavy loss beyond ring depth): `get()` → `nullptr` → keyframe re-seed. Client must accept a keyframe even while holding a stale baseline (keyframe replaces unconditionally).
- **Reordered/duplicate inputs**: `max()` on `last_received_tick` — never regress the baseline.
- **Reordered snapshots on client**: a late delta whose `baseline_tick` ≠ held tick is dropped; keyframes always apply (self-contained).
- **Empty world / zero entities**: keyframe `count=0`; delta `changed=0, removed=0`. Must round-trip.
- **Removed entity**: id present in baseline, absent in current → removed list; client erases it. (No server trigger today; format-only.)
- **u32 tick wraparound**: ~2.27 yr of continuous uptime @ 60 Hz; not handled, noted.
- **u16 count cap**: 65535 entities — same limit as today's full-state format; unchanged.

---

## Why this approach

- **Delta against a client-confirmed baseline (Quake3/Source model).** Over lossy UDP with no reliable snapshot channel, you can only diff against state you *know* arrived — so the client must tell the server what it has. Considered and rejected: (a) piggybacking the transport's packet ack — it inverts layering (transport reaching into sim concepts) and acks "datagram arrived," not "snapshot adopted," which diverge under fragmentation; (b) a reliable snapshot channel — pointless for latest-wins state, and it would head-of-line block.
- **App-level ack** keeps the dependency one-way (sim → transport) and acks the right event. Cost is ~4 bytes on the cheap upstream direction — negligible.
- **Entity-level granularity** is naive-first: on `{id,x,y}` with all-moving bots a field mask saves nothing, so building it now would be a format written twice. The bit-packing slice introduces masks when they pay.
- **Keyframe fallback over delta retransmission**: a stale snapshot is worthless (latest-wins), so re-seeding with a fresh full state is simpler and cheaper than resending old deltas.
- **Tradeoffs accepted**: (1) breaks the serialize-once broadcast → per-client serialization, O(clients × entities) CPU, for ~0 bandwidth gain on this workload — accepted because the machinery is the deliverable and the measured cost *is* the finding; (2) `SnapshotHistory` memory is O(depth × entities).

## Risks / what could go wrong

- **Per-client serialization becomes the server hot path.** The shared-blob broadcast (`server/main.cpp:138-145`) becomes one serialize per client — O(clients × entities) memcpy each tick. At N=100 that is ~100× the serialization work for no bandwidth win and could drop the tick rate before AOI lands. Mitigation: watch `netcode_tick_rate_hz` during the measurement run; if it sags, that itself is a documented finding.
- **Bots that don't ack break the measurement silently.** If the bot client fails to advance `last_received_tick`, the server sends 100% keyframes and the dashboard shows delta ≈ full *for the wrong reason*. The `netcode_delta_packets_total` counter being non-trivial is the guard — it must be checked, not assumed.
- **Baseline desync corrupts client state with no error.** A delta applied against the wrong baseline drifts entities permanently and silently. Mitigation: strict `base.tick == baseline_tick` equality before applying; keyframe re-seed is the only recovery. Covered by a unit test.
- **Input format bump footgun.** `encode_input` currently raw-`memcpy`s the struct; switching to explicit encode + adding `last_received_tick` must keep server `decode_input` in exact lockstep, or inputs misparse and bots "move" on garbage with no crash. Covered by an input round-trip test.
- **Ring depth vs ack latency.** If depth (~128 ticks) is shorter than a slow client's confirm latency under high simulated loss/latency, that client thrashes on keyframes. Depth is a tuning knob; flagged for the measurement settings.

## Success criteria

- All existing tests stay green (55/55) plus the new ones below.
- **Codec round-trip (unit):** for empty world, single entity, many entities, a tick with an add, a tick with a removal, and a no-change tick — `encode_keyframe → apply` and `encode_delta(curr, baseline) → apply(baseline)` reproduce `curr` exactly (ids and `x,y` bit-equal).
- **Baseline-mismatch rejection (unit):** `apply_snapshot` of a delta returns `false` and leaves state untouched when held tick ≠ `baseline_tick`.
- **History (unit):** `push/get/age-out` behave; `encode_for` returns a keyframe when the baseline is absent and a delta when present.
- **Input round-trip (unit):** `encode_input → decode_input` preserves `dx, dy, last_received_tick`.
- **End-to-end (measurement run, N=20):** server + bots run, and `netcode_delta_packets_total` climbs to a non-trivial share of total snapshot packets (the ack loop closes — not 100% keyframes); `netcode_tick_rate_hz` holds ~60.
- **Headline number:** at N=50 and N=100, delta egress KB/s ≈ the P2 full-state baseline (within ~5–10%, confirming the predicted ~0 win at 100% churn), captured alongside the existing baseline table; tick rate recorded (the CPU-cost finding).

## Test extensions required

- New `tests/snapshot_delta_test.cpp` — the codec round-trip + baseline-mismatch cases above.
- New `tests/snapshot_history_test.cpp` — ring push/get/age-out and `encode_for` keyframe-vs-delta selection.
- Extend an input test (or add one) for the `encode_input/decode_input` round-trip with `last_received_tick` set.
- `tests/world_test.cpp`: no change — `World` is untouched.
- No new automated integration/harness test: the harness is a manually-driven measurement tool, and the end-to-end ack loop is covered by the measurement-run success criterion plus full unit coverage on the codec and history. (Justified per workflow rule 4 — this is not "I'll test manually" hand-waving; the codec, the layer where bugs hide, is unit-tested; the harness only wires it.)

---

## Decisions during implementation

<!-- Append-only; dated; non-obvious choices made while building. -->

### 2026-06-07 — Removed the legacy `encode_snapshot`/`decode_snapshot` instead of keeping them
The spec said to keep the untagged full-state pair untouched so nothing breaks. Once the server migrated to the `SnapshotHistory.encode_for` path, the only remaining users were two `world_test.cpp` cases, so keeping them would have left dead, tested-but-unused encoders (a second snapshot format a reviewer would have to reconcile with the tagged one). Removed both; migrated `World.FullStateSnapshotRoundTrips` to `encode_keyframe`/`apply_snapshot` (preserving the World→wire integration angle) and dropped `Snapshot.DecodeRejectsTruncatedBuffer` as redundant with `snapshot_delta_test`'s truncation case. The legacy single-entity `Snapshot` struct stays — the P0 relic client still depends on it.

## Spec amendments

<!-- Append-only; dated; times the spec was wrong mid-build. -->

---

## Future work (out-of-scope ideas surfaced during this feature)

- Idle/static "scenery" entities (or a fraction-idle bot mode) so delta also produces a *positive* number proving the mechanism, without tainting the all-moving headline load.
- Split egress bytes by keyframe vs delta (not just packet counts) for a sharper dashboard.
- Disconnect detection so the removed-id path is actually exercised end-to-end.
- Field-level delta + changed-field masks — folds naturally into the bit-packing slice.
