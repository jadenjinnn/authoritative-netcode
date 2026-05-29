# Architecture

> Living document, updated via `/architecture-update`. **Status: pre-build TARGET** — this
> is the hypothesis we're building toward, not a description of existing code. Sections
> firm up as each phase lands.

## One-paragraph overview
An authoritative game server and matching client communicating over a custom reliable-UDP
transport. The transport (Layer 0) is a standalone library that doesn't know it's used for
a game. On top sit a state-sync protocol (Layer 1: snapshots → delta → bitpack → AOI) and a
simulation/prediction layer (Layer 2: fixed-tick authoritative sim, client prediction,
reconciliation, lag compensation). Measurement (metrics, bot load harness, debug HUD) is
cross-cutting — it taps every layer rather than sitting on top. Headline design choice:
authoritative server + client-side prediction over hand-rolled reliable-UDP, with every
optimization gated on a measured baseline.

## Layered model

```
   Client (sim + predict + render)          Server (authoritative sim)
           \_______________ Transport (L0) ________________/
             reliable-UDP: seq/ack, channels, frag,
             handshake, congestion control
                   | artificial-network shim (latency/loss/jitter/reorder),
                   |   wraps the BSD socket, runtime-toggleable
           State-sync protocol (L1): snapshot → delta → bitpack/quantize → AOI
           Simulation (L2): fixed-tick auth sim · prediction · reconciliation · lag-comp
   Cross-cutting: metrics (prometheus-cpp) · headless bot harness · client debug HUD
```

## Module map (target)
- `transport/` — standalone reliable-UDP library (own README + tests)
- `transport/net/` — BSD socket wrapper + artificial-network shim
- `protocol/` — L1 state-sync: snapshot / delta / bitstream / AOI
- `sim/` — L2 shared deterministic simulation (used by both server and client predict)
- `server/` — authoritative server: tick loop, per-client state, snapshot send
- `client/` — game client: predict, reconcile, interpolate, render, debug HUD
- `bots/` — headless bot client + N-client controller (uses the same transport)
- `metrics/` — prometheus-cpp registration/export
- `tests/` — GoogleTest suites (transport core, protocol, sim)
- `docs/` — architecture, decisions.md, features/, bugs/

## Data flow (steady state, per tick)
1. Client samples input, applies it locally (prediction), stamps it with an input seq,
   buffers it unacked, and sends over the unreliable channel.
2. Server tick loop (fixed timestep) drains inputs, validates them, advances the
   authoritative sim by one tick.
3. Server builds each client's snapshot (AOI-filtered), deltas it against that client's
   last acked baseline, bit-packs it, sends over the unreliable-ordered channel.
4. Client receives a snapshot, reconciles (rewind to acked tick, replay unacked inputs),
   interpolates remote entities ~100ms in the past, renders.
5. On a fire event, server rewinds to the world state the firing client perceived
   (interpolation delay + RTT/2) and validates the hit there (lag compensation).
6. Metrics emitted throughout; bot harness drives steps 1/4 headlessly under load.

## Build system
- Top-level `CMakeLists.txt`; subdirectories per module above, added as each phase lands.
- C++17. GoogleTest + prometheus-cpp via FetchContent (added when first needed).
- Output: `server`, `client`, `bots` binaries; `transport` library + its tests.

## Open questions (decide later — do NOT pin now)
- Server I/O model: single-threaded blocking `recvfrom` in the tick loop (skeleton-fine)
  → `epoll` / non-blocking when bot count demands it. Decision entry when we switch.
- Tick rate: 30 vs 60 Hz. Decide at first sim work.
- Client renderer lib: raylib (simplest) vs SDL2 vs SFML. Skeleton is headless, so defer.
- Entity/ID scheme + quantization params (world size, precision bits). Defer to L1.
- prometheus-cpp vs a hand-rolled exposition endpoint. Confirm at P2.
- Packaging: Docker Compose to stand up server + bots + Prometheus + Grafana as a one-command
  reproducible measurement harness — run the *measured* server on host networking so the latency
  numbers stay clean. Not Kubernetes: its NAT/overlay networking fights low-latency UDP (the
  reason Agones exists) and would taint the results table. Confirm at P2.
- Threading model for snapshot serialization (per-client parallel?). Defer; measure first.
