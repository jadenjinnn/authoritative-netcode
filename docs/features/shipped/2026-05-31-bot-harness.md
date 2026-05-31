# Feature spec: Headless bot harness + multi-client authoritative server

> Tier: **Full spec**
> Status: **shipped**
> Started: 2026-05-31
> Spec author: Jaden Jin

---

## Classification

- **Reversibility**: Sticky
- **Scope**: Cross-cutting
- **Tier rationale (1 sentence)**: Rewrites the server's core loop from single-client raw-socket to multi-client Peer fan-out and turns `World` into a dynamic multi-entity sim, touching server/sim/transport plus a new app — sticky and cross-cutting, the top-right of the matrix.

---

## Problem

P2's deliverable is a measured number: the naive full-state bandwidth baseline under load, the thing P3 (delta compression, bit-packing, area-of-interest) exists to beat. We can't measure it yet — the server is a single-client toy (`std::optional<Endpoint>`, raw `Snapshot` struct over a raw `UdpSocket`, a one-entity world that auto-moves and ignores input), and there's no way to put load on it. This slice makes the server a real authoritative multi-client server riding `Peer`, and builds a headless bot swarm to drive it, so we can state "at N players the naive broadcast costs X" — and have that X be the dramatic O(N²) number that motivates the whole P3 arc.

## In scope

- **Multi-client server on `Peer`.** A `PeerManager` owns one `Peer` per remote endpoint (`map<Endpoint, Peer>`); the server's tick loop drains all peers, applies inputs, steps the sim, and broadcasts full state to every peer. Replaces the single-client raw-socket path.
- **Multi-entity `World`.** One entity per connected client, added on connect and removed on disconnect; the existing auto-mover stays as a neutral ambient entity is **not** kept (see Decisions) — entities are player-driven.
- **`Input` message type.** A small struct (intended direction) sent unreliable, applied to the sender's entity each tick. This is what makes the server authoritative: clients send inputs, the server simulates, the server broadcasts state.
- **Reliable path exercised.** A `join` on connect and a periodic discrete "event" ride the reliable channel, so the bot harness exercises Peer's reliable + mux/demux end-to-end (not just the unreliable path).
- **Bot harness app** (`bots/`). Spawns N bots in one process; each owns a `Peer` to the server, connects, and drives traffic at tick rate.
- **Bot input-driver state machine** — **Jaden's hand-written piece this slice.** Per-tick, decides what a bot emits: an unreliable `Input` every tick (following some movement pattern), plus an occasional reliable event.
- **Baseline measurement.** The server prints downstream full-state egress (bytes/s and packets/s, per-client and total) at steady state; the harness can be run at a few N values to produce the table. Reliable-upstream volume reported separately so it doesn't muddy the broadcast number.

## Out of scope

- **Prometheus / Grafana export.** This slice prints to stdout. Metrics export + the dashboard is the next P2 slice (CLAUDE.md lists it separately under the measurement spine).
- **Client prediction / reconciliation / clock sync** (P4). Bots are dumb input generators; they do not predict or correct.
- **Redundant input packing** (last-N inputs per packet) — a P4 reliability-under-loss concern, not a baseline concern.
- **Delta compression, bit-packing, quantization, AOI** (P3) — this slice deliberately ships the *naive* full-state broadcast that P3 optimizes.
- **Lag-compensated hit detection / input validation / threat model** (P5).
- **The real client app** (`client/`) is left as-is; it stays a simple raw-socket listener. Migrating it onto Peer is not required for the baseline and would widen the diff.
- **Docker Compose packaging** — confirmed for later in P2 (stands up Prometheus/Grafana); not needed to print a number.

## API sketch

```cpp
// --- transport/reliable/peer_manager.h ---
// Server-side fan-out: one Peer per remote, created on first contact.
class PeerManager {
public:
    PeerManager(net::ISocket& socket);
    // Drain the socket once; route each datagram to the owning Peer (creating one
    // on first contact). Returns the endpoints that delivered messages this drain,
    // so the caller can pull each peer's Delivery.
    std::vector<net::Endpoint> poll(uint64_t now_us);
    Peer& peer(const net::Endpoint& who);
    void update_all(uint64_t now_us);          // pace + flush every peer
    const std::map<net::Endpoint, Peer>& peers() const;
    // Egress totals across all peers, for the baseline print.
    uint64_t total_bytes_sent() const;
};

// --- sim/input.h ---
struct Input {
    float dx = 0.0f;   // intended velocity direction, [-1, 1]
    float dy = 0.0f;
};

// --- sim/world.h additions ---
using EntityId = uint32_t;
EntityId World::add_player();           // returns id of the new entity
void      World::remove_player(EntityId);
void      World::apply_input(EntityId, const Input&);
// step(dt) integrates every entity; full state = all entities.

// --- bots/bot.h: the hand-written driver ---
class BotDriver {
public:
    // Called once per bot tick. Returns what to send this tick.
    struct Emit {
        Input input;                 // unreliable, every tick
        std::optional<uint32_t> event;  // reliable, occasionally (nullopt = none)
    };
    Emit tick(uint64_t now_us);
};
```

```cpp
// Bot harness loop (one process, N bots), sketch:
for (auto& bot : bots) {
    BotDriver::Emit e = bot.driver.tick(now);
    bot.peer.queue_unreliable(encode(e.input));
    if (e.event) bot.peer.queue_reliable(encode(*e.event));
    bot.peer.update(now);
    // ... poll for state, pump sim socket if used ...
}
```

---

## Design

### Data structures

- **`PeerManager`** (`transport/reliable/peer_manager.{h,cpp}`) — owns `std::map<net::Endpoint, Peer>` and the server's `ISocket&`. The map keys on `Endpoint`; needs `operator<` for `Endpoint` (add to `endpoint.h` — a value comparison on ip+port). One shared receive buffer; `poll` reads datagrams and dispatches by source endpoint, creating a `Peer` on first contact.
- **`Input`** (`sim/input.h`) — `{float dx, dy}`. POD, memcpy-serialized like `Snapshot`.
- **`World`** multi-entity — replace the single `player_` with `std::map<EntityId, Entity>` (stable ids across add/remove). `add_player` assigns the next id; `apply_input` sets the target entity's velocity from the input; `step` integrates all. Full-state snapshot becomes a count-prefixed list of `{EntityId, x, y}`.
- **`Snapshot`** grows from one entity to a list. New wire shape: `[uint32 tick][uint16 count]{ [EntityId][float x][float y] }*`. This is the naive full state — every entity, every tick, to every client.
- **`BotDriver`** (`bots/bot.{h,cpp}`) — the hand-written state machine; holds whatever per-bot state the movement pattern needs (e.g. current heading, a tick counter for the periodic event). Pure decision logic: `tick(now)` returns an `Emit`, touches no sockets.
- **Bot** (harness-side) — bundles a `Peer`, a `BotDriver`, and the bot's view of received state.

### Module touchpoints

- **New:** `transport/reliable/peer_manager.{h,cpp}`; `sim/input.h`; `bots/bot.{h,cpp}`, `bots/main.cpp`, `bots/CMakeLists.txt`.
- **`transport/net/endpoint.h`** — add `operator<` (and `operator==`) for use as a map key.
- **`sim/world.{h,cpp}`** — single entity → entity map; add `add_player`/`remove_player`/`apply_input`/`EntityId`.
- **`sim/snapshot.h`** — single entity → entity list (or a `serialize_state` helper).
- **`server/main.cpp`** — rewrite the loop onto `PeerManager`: poll peers, apply inputs, step sim, broadcast full state, handle connect/disconnect, print egress.
- **`transport/reliable/CMakeLists.txt`** — add `peer_manager.cpp`.
- **Root `CMakeLists.txt`** — `add_subdirectory(bots)`.
- **`tests/`** — `peer_manager_test.cpp`, `world_test.cpp` (multi-entity + input), `bot_driver_test.cpp` (the hand-written SM). New test targets.
- **No change** to `client/`, or to the Peer/Connection/channel internals (PeerManager composes Peer; it doesn't modify it).

### Lifecycle / control flow

Server tick (fixed 60 Hz, same accumulator):
1. `mgr.poll(now)` — drain socket, route datagrams to per-endpoint Peers (new endpoint → new Peer → `world.add_player()`, map endpoint→EntityId).
2. For each peer with a delivery: pull `Delivery`; apply each unreliable `Input` to that endpoint's entity; handle reliable events (join/discrete) — log/count them.
3. Advance the sim the due number of fixed steps (`world.step(dt)` integrates all entities).
4. Build the full-state snapshot once; `queue_unreliable` it to **every** peer; `mgr.update_all(now)` paces + flushes. (This is the O(N²) broadcast — each of N snapshots carries N entities.)
5. Accumulate egress counters; periodically print bytes/s, packets/s (per-client avg + total).
6. Disconnect: a peer silent for a timeout → `world.remove_player(id)`, drop the Peer. (Bots in this harness run to completion, so timeout-disconnect is a correctness backstop, lightly exercised.)

Bot tick (harness, N bots in one loop):
1. `driver.tick(now)` → `Emit{input, event?}`.
2. `peer.queue_unreliable(encode(input))`; if `event`, `peer.queue_reliable(encode(event))`.
3. `peer.update(now)`; `peer.poll(now)` to receive state (counted, not acted on — bots don't predict).
4. The harness drives a virtual or real clock uniformly across all bots.

### Edge cases

- **Two bots from the same source endpoint** — can't happen in-process (each bot binds its own socket), but `PeerManager` keys on endpoint, so it's correct by construction: one endpoint = one Peer = one entity.
- **First datagram from an unknown endpoint that isn't a join** — still creates a Peer + entity (any contact is a connection). The reliable `join` is a courtesy/handshake marker, not a gate; the server doesn't drop non-join first contact. (Keeps it simple; a real handshake is later.)
- **Disconnect mid-run** — entity removed, Peer dropped; a late datagram from that endpoint re-creates a fresh Peer+entity (acceptable for the baseline; no session identity yet).
- **EntityId reuse / wraparound** — ids are monotonic `uint32`; at baseline scale (hundreds of bots, minutes) wraparound is irrelevant. No reuse within a run.
- **Empty world** (no bots connected) — server ticks, broadcasts nothing, egress 0. Fine.
- **Snapshot exceeds one datagram at high N** — N entities × 12 bytes crosses the 1024-byte fragment threshold around ~85 entities, so Peer's fragmentation fires on the broadcast. This is correct and worth noting: the baseline measurement implicitly exercises fragmentation at scale.
- **Bot driver emits an event every tick** (degenerate) — would flood the reliable channel; the SM must gate events to "occasional." This is a property the `bot_driver_test` should pin.

---

## Full spec

### Sequence diagram / data flow

```
Bot (N of them)                     Server (60 Hz authoritative loop)
--------------                      ---------------------------------
driver.tick() -> Emit
queue_unreliable(Input) ───────────▶ mgr.poll(): new endpoint? -> Peer + world.add_player()
queue_reliable(join)    ───────────▶                  pull Delivery per peer
peer.update() [flush]                                  apply_input(entity, Input)
                                                       handle reliable join/event
                                     world.step(dt) x steps_due   (integrate ALL entities)
                                     snapshot = serialize(all entities)   <-- full state
                  ◀─────────────────  for each peer: queue_unreliable(snapshot)
                                     mgr.update_all() [flush to all]  <-- O(N^2) broadcast
peer.poll() <- state (counted)
                                     accumulate + print egress: bytes/s, pkts/s, per-client & total
```

The measured quantity is the downstream arrow: one snapshot of N entities, sent to each of N peers, every tick. Upstream (inputs + occasional events) is reported separately.

### Error handling

- **Unknown / malformed datagram** — Peer's `deserialize` already returns false on a truncated packet (dropped like loss); PeerManager logs and continues. No crash on garbage.
- **Send failure** (`send_to` returns short / errors) — counted as a dropped send; the loop continues (UDP is lossy by nature; one failed send is indistinguishable from wire loss).
- **Bot can't reach server** (wrong port) — bot gets no state; harness prints "0 snapshots received" rather than hanging. A connect timeout bounds startup.
- **Disconnect detection** — a peer with no received datagram for `kPeerTimeoutMs` is reaped; its entity removed. Reaping is best-effort (checked each tick), not exact.
- Errors are surfaced via stdout counters (this is a measurement tool, not a service); no exceptions across the API boundary.

### Performance considerations

- **The baseline IS the performance story.** Full-state broadcast is O(N²) in bytes/tick: N snapshots × N entities × ~12 bytes × 60 Hz. Expected ballpark: at N=100 that's ~100×100×12×60 ≈ **72 MB/s** total server egress — the deliberately-bad number P3 dismantles. The spec's success is *producing and reporting* this number, not minimizing it.
- **Server tick budget.** Building one snapshot per tick is O(N); fanning it to N peers (serialize+fragment+send) is O(N²) work *and* bytes. At the N we measure (target up to ~100–200 bots on one box), the loop must still hold 60 Hz — if it can't, that's itself a finding (report achieved tick rate alongside the bandwidth). No premature optimization; measure first.
- **One snapshot serialized once, reused per peer** — avoid re-serializing identical full state N times. (Per-peer delta would defeat the "naive" baseline; identical-blob reuse is just not being wasteful.)
- **Bots in one process** — N Peers + N sockets in a single loop. Fine to low-hundreds; if the harness itself becomes the bottleneck before the server does, note it (the measurement must be server-bound to be valid).

### Migration / compatibility

- **Breaks the current `client/`↔`server/` raw-`Snapshot` contract.** The server moves to Peer-framed, multi-entity, list snapshots; the existing `client/` (raw single-`Snapshot` reader) will no longer parse server output. Accepted: `client/` is a P0 relic kept only as a manual smoke tool; it's explicitly out of scope to migrate, and the bot harness is the real client now. We note in the server's startup banner that it speaks the Peer protocol.
- No persisted data / save format, so no data migration.
- `World`'s single-entity API (`player()`) is replaced; the only caller is `server/main.cpp` (rewritten here) and `client/` (raw, untouched, doesn't call World). Low blast radius.

---

## Why this approach

- **Each bot is an entity (vs. bots as pure traffic generators against a 1-entity world).** Only the entity-per-bot version produces a baseline that *means* something: full state scales with player count, so the broadcast is O(N²) and obviously unshippable — which is the entire motivation for P3's delta/AOI work. A 1-entity world gives a flat ~12-byte snapshot that P3 would have nothing to optimize; the measurement would be theater. Cost accepted: `World` must go multi-entity and inputs must actually move entities.
- **Both channels driven (vs. unreliable-only).** Authority is demonstrated by the input→sim→state loop alone, and inputs correctly ride the *unreliable* channel (a dropped input is obsolete before a resend would arrive). But the harness is Peer's first real multi-client consumer; driving only the unreliable path would leave the reliable channel + mux/demux (a whole P1 slice) unexercised on a real path, and is unrealistic (real clients send joins and discrete events reliably). So: unreliable input every tick + occasional reliable event. The headline number stays the downstream broadcast; reliable upstream is reported separately.
- **`PeerManager` as the fan-out (vs. baking multi-client into Peer).** Peer is deliberately one-connection; multi-client is a separate responsibility (routing datagrams by source, lifecycle of connections). A thin `PeerManager` over `map<Endpoint,Peer>` keeps Peer single-purpose and is the deferred piece the Peer spec named. Rejected: a multi-remote Peer (conflates connection state with a connection registry).
- **Print to stdout now, Prometheus next slice (vs. wiring metrics in here).** The number is the deliverable; the export pipeline is a distinct concern with its own dependencies (prometheus-cpp, Grafana). Folding it in would make an already-large sticky slice larger. Separating them keeps each reviewable.
- **Leave `client/` alone (vs. migrate it onto Peer too).** The bot harness is the real client for measurement; the P0 `client/` is a manual smoke relic. Migrating it adds diff for no baseline value.

## Risks / what could go wrong

- **Scope: this is the biggest slice yet and it's sticky.** It rewrites the server loop, changes `World`'s core data model, and adds an app — four moving parts. Risk: it sprawls or half-lands. Mitigation: build in dependency order with a working system at each step — (1) `Endpoint` ordering + `PeerManager` + its test, (2) multi-entity `World` + `Input` + tests, (3) server rewrite (smoke: one real bot connects, moves, gets state), (4) bot harness + driver, (5) the measurement print. Each step compiles and tests green before the next.
- **The harness becomes the bottleneck, not the server.** If N Peers in one process can't push input fast enough, the measured server egress is harness-limited and the baseline is wrong. Mitigation: report achieved server tick rate and per-bot send rate alongside bandwidth; if the harness saturates first, say so and cap N at the honest number. Multi-process bots are a later option, not now.
- **Snapshot fragmentation at high N changes the per-packet cost.** Past ~85 entities the broadcast fragments, so bytes/packet and packet counts jump non-linearly. Risk: misreading the number. Mitigation: report bytes/s (the real cost) as primary, and note the N at which fragmentation begins.
- **`World` API churn breaks the build in non-obvious places.** The single-entity `player()` accessor is referenced by `server/` and possibly demos. Mitigation: grep all callers before changing; the only sim consumer is `server/main.cpp`.
- **Disconnect/entity-leak.** If a bot never cleanly disconnects, its entity lingers and inflates the broadcast. For a fixed-duration measurement run this is benign, but a leaked entity would silently skew a long run. Mitigation: timeout-based reaping + a `world_test` that add/remove keeps the entity set correct.
- **Floating-point snapshot determinism across the wire** — not a correctness risk for the baseline (we measure bytes, not exact positions), but if `bot_driver_test` asserts on positions it could be flaky. Mitigation: the driver test asserts on *emitted intent* (Input/event cadence), not on resulting world positions.

## Success criteria

- **Builds clean; full suite green** — existing 40 tests plus new `peer_manager_test`, `world_test` (multi-entity + input application), and `bot_driver_test` (the hand-written SM's cadence: an Input every tick, events only occasionally, movement pattern well-formed).
- **End-to-end smoke:** running the server + the bot harness with N=1 shows the bot connecting, the server creating an entity, the bot's input moving that entity, and the bot receiving state snapshots (counts > 0 on both sides).
- **Baseline produced:** the server prints steady-state downstream egress (total bytes/s + per-client bytes/s + packets/s) at, at minimum, N ∈ {1, 10, 50, 100} (or the honest max the box sustains at 60 Hz). The numbers show the O(N²) growth and are recorded for the P2 results table. Achieved server tick rate is reported alongside so the measurement is known to be server-bound.
- **Reliable path proven on a real multi-client path:** the server reports reliable events received from bots (join + periodic events), confirming Peer's reliable channel + mux/demux work under the harness, not just in unit demos.

## Test extensions required

- **`tests/peer_manager_test.cpp`** — first contact from a new endpoint creates exactly one Peer; datagrams route to the correct Peer by source; two endpoints get two independent Peers; egress totals sum across peers. Uses the in-memory `FakeSocket` pattern from `peer_test.cpp` (extended to carry a source endpoint).
- **`tests/world_test.cpp`** — `add_player` returns distinct ids; `apply_input` moves only the targeted entity; `step` integrates all entities; `remove_player` drops it from the full-state set; full-state serialize/deserialize round-trips an N-entity snapshot.
- **`tests/bot_driver_test.cpp`** — the hand-written SM: emits an `Input` every tick; emits a reliable event only on its defined cadence (not every tick, not never); the movement pattern produces bounded, varied directions (defendable as "representative load"). Asserts on emitted intent, not on world state.
- No changes to existing tests (Peer/Connection/channel internals untouched). The server loop itself is covered by the end-to-end smoke, not a unit test (it's an integration/measurement binary).

---

## Decisions during implementation

### 2026-05-31 — Measured baseline (localhost, 60 Hz, build=Debug)
The P2 deliverable. Naive full-state broadcast, server egress as reported by `PeerManager::total_bytes_sent`:

| Clients (N) | Total egress | Per client | Reliable events |
|---|---|---|---|
| 1   | 1.8 KB/s   | 1.8 KB/s  | arriving |
| 50  | ~1.8 MB/s  | 36.3 KB/s | arriving |
| 100 | ~7.0 MB/s  | 71.6 KB/s | arriving |

The O(N²) shape is visible: per-client egress doubles 50→100 (each snapshot carries 2× the
entities) and total goes ~4× as clients also double. This is the curve P3 (delta compression,
quantization, AOI) exists to beat. Caveat: the per-client figure includes Peer's empty
heartbeat flushes and the input echo, so it slightly overstates pure snapshot payload — the
*shape*, not the absolute, is the point, and P3 measures against this same harness.

### 2026-05-31 — PeerManager routes via per-Peer in-memory inbox sockets
A `Peer` drains its own `ISocket` and treats everything read as from its one remote, so peers
can't share the server's socket directly. `PeerManager` owns the real socket, drains it once
per `poll`, and routes each datagram by source endpoint into that peer's private `Inbox`
(an `ISocket` over a queue); the Peer then drains the inbox. Sends pass straight through to the
shared socket. Peer is unmodified — the deferred multi-client piece composes it rather than
changing it. (`Conn` is held by `unique_ptr` so the Peer's reference to its Inbox stays stable.)

### 2026-05-31 — Server stdout line-buffered for measurement runs
Measurement runs end by SIGTERM (the harness kills the server), which skips stdio flush, so a
fully-buffered stdout lost the egress report. `setvbuf(_IOLBF)` makes each periodic line land.

### 2026-05-31 — Drop the auto-moving ambient entity
The P0 `World` had one entity with a hardcoded `vx=20` that bounces off bounds. With entities now player-driven, that ambient mover is removed — every entity is a connected bot driven by its input. Keeping a server-side autonomous entity would muddy the baseline (state that no client produces) and serve no purpose. If a future demo wants ambient NPCs, that's a separate, intentional addition.

## Spec amendments

<!-- Append-only. Dated entries when the spec was wrong and changed mid-build. -->

---

## Future work (out-of-scope ideas surfaced during this feature)

- Prometheus/Grafana export of the egress + tick-rate counters (next P2 slice).
- Multi-process bot harness if single-process N saturates before the server does.
- A real connect/disconnect handshake with session identity (so a reconnect resumes rather than spawning a fresh entity).
- Migrate the `client/` app onto Peer + the new snapshot format (currently left as a raw relic).
- Redundant input packing (last-N inputs per packet) — P4, for input survival under loss.
