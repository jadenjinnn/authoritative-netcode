# Authoritative Networked Game Server — Project Spec

> A portfolio project to demonstrate systems-engineering depth: custom reliable-UDP transport, authoritative server simulation, client-side prediction, lag compensation, and production-shaped measurement infrastructure.

---

## Context for Claude Code

I'm building this as the second project in a systems-engineering-focused SWE portfolio. My first project is **Hamster**, a from-scratch 2D game engine (C++/ECS/batched 2D rendering/spritesheets/editor UI). This project must clearly differentiate from Hamster — the headline here is the **networking stack**, not the game. The game is a deliberately minimal test harness.

I have ~3 months. I want every resume bullet to be backed by a real measured number, with a defensible baseline.

When you help me, please:
- Push back if I scope-creep into game features. The game is a harness.
- Prioritize correctness, measurement, and clean layering over feature count.
- Treat the transport library as a standalone artifact with its own tests.
- Help me build measurement infrastructure *first*, not as an afterthought.

---

## One-line description

Authoritative real-time game server with a custom reliable-UDP networking stack, client-side prediction, server reconciliation, and lag-compensated hit detection — measured under load with realistic simulated network conditions.

---

## Goals & non-goals

### Goals
- Build a custom reliable-UDP transport library (standalone, reusable, well-tested)
- Build an authoritative server with delta-compressed snapshot protocol and interest management
- Implement client prediction, server reconciliation, and lag compensation
- Build measurement infrastructure (metrics, dashboards, headless bot load-tester, artificial network conditions) from day one
- Produce defensible, measured numbers for every resume bullet

### Non-goals (resist these)
- No fancy game art, sound, UI polish on the *game itself*
- No matchmaking, lobbies, accounts, persistence
- No browser/web client (defeats the UDP point)
- No physics replication across clients (keep gameplay simple)
- No ECS architecture as a "feature" (incidental at most; Hamster already shows this)
- No premature optimization — build naive baseline, measure, then optimize

---

## Stack

- **Language:** C++ (consistent with Hamster, gives real memory/perf control)
- **Build:** CMake
- **Metrics:** Prometheus client lib + Grafana
- **Testing:** Catch2 or GoogleTest
- **Platform:** Linux primary, cross-platform if cheap

---

## Architecture — four layers

```
┌─────────────────────────────────────────────────────────┐
│ Layer 3: Measurement & Load Testing                     │
│  - Headless bot client harness                          │
│  - Artificial network conditions (latency/loss/jitter)  │
│  - Prometheus metrics + Grafana dashboards              │
├─────────────────────────────────────────────────────────┤
│ Layer 2: Simulation & Prediction                        │
│  - Fixed-timestep server tick loop (authoritative)      │
│  - Client input prediction + buffering                  │
│  - Server reconciliation (rewind + replay)              │
│  - Lag compensation for hit detection                   │
│  - Input validation / threat model                      │
├─────────────────────────────────────────────────────────┤
│ Layer 1: State Sync Protocol                            │
│  - Snapshot serialization                               │
│  - Delta compression against acked baseline             │
│  - Bit packing + quantization                           │
│  - Interest management (spatial AOI)                    │
├─────────────────────────────────────────────────────────┤
│ Layer 0: Reliable-UDP Transport (standalone library)    │
│  - Sequence numbers + ack bitmasks                      │
│  - Mixed-reliability channels                           │
│  - Fragmentation/reassembly                             │
│  - Connection handshake (challenge-response)            │
│  - Congestion control (RTT-based)                       │
└─────────────────────────────────────────────────────────┘
```

---

## Layer 0 — Reliable-UDP Transport

A standalone library that doesn't know it's being used for a game.

### Techniques
- **Sequence numbers + 32-bit ack bitmask** per channel (Glenn Fiedler's pattern)
- **Channels with mixed reliability:**
  - Unreliable (e.g. inputs — drop old, don't retransmit)
  - Unreliable-ordered (e.g. snapshots — drop out-of-order)
  - Reliable-ordered (e.g. chat, RPC, connection events)
- **Packet fragmentation/reassembly** for payloads > MTU (~1200 byte safe limit)
- **Connection handshake** with challenge-response (prevents trivial spoofing/amplification)
- **Congestion control** — RTT-based good/bad mode switching
- **Endianness-safe bitstream serialization** (write our own bit reader/writer)

### Artificial network layer (build inside this library)
Configurable injection of:
- Latency (constant + jitter)
- Packet loss (%)
- Packet reordering
- Duplicate packets

Toggle via config. This is how we reproduce realistic conditions on localhost AND how we demo the project visually (slider for latency, watch prediction hold up).

### Testing
- Unit tests for sequence-number arithmetic (wraparound!)
- Unit tests for ack bitmask logic
- Integration tests using the artificial network layer (assert reliable channel delivers all messages under 30% loss, etc.)

---

## Layer 1 — State Sync Protocol

Built on top of Layer 0's unreliable-ordered channel.

### Techniques
- **Delta compression against last-acked baseline.** Server keeps a small per-client ring buffer of recent snapshots; sends delta from the most recent one the client has acked. Fall back to full snapshot if the client's acked baseline is too old.
- **Bit packing + quantization.** Positions don't need 32-bit floats. If world is 1024 units and precision is 0.01, that's 17 bits. Angles: 8–12 bits. Velocities: fewer bits than positions.
- **Interest management (AOI).** Uniform spatial grid (don't reach for quadtree until measured insufficient). Each client only receives entities within its AOI radius.
- **Snapshot interpolation on the client.** Render ~100ms in the past, interpolate between two received snapshots. Prediction is only for the *local* player.
- **Baseline negotiation fallback.** If client's acked snapshot is too stale, send a keyframe / full snapshot for that client only.

### Build naive baseline FIRST
Ship "send full state every tick to every client" version. Measure bandwidth. *Then* add delta compression, measure again. *Then* bit packing, measure again. *Then* interest management, measure again. Each step is a defensible number with a clear baseline.

---

## Layer 2 — Simulation & Prediction

### Techniques
- **Fixed-timestep server tick** (30 or 60 Hz). Accumulator pattern to decouple from wall clock.
- **Client input prediction.** Client applies inputs locally immediately, stamps each with a sequence number, buffers unacked inputs.
- **Server reconciliation.** When authoritative snapshot arrives, client rewinds local state to that snapshot's tick and replays all unacked inputs forward. This is what makes movement feel instant.
- **Lag compensation for hit detection.** Server keeps ring buffer of past world states (200–500ms). On a fire event, server rewinds to the state that client *saw* when they pulled the trigger (using client's interpolation delay + RTT/2), validates the hit there, applies forward.
- **Server-side input validation.** Clamp movement to max-speed-per-tick, validate fire rate, never trust client-reported position. Closes speed/teleport cheat class by construction. **Write threat model as a document in the repo.**
- **Clock synchronization.** Simple NTP-style RTT-halving handshake, run periodically, smoothed estimate.

### Game scope (deliberately tiny)
- Top-down 2D arena
- Players are circles/capsules with a directional indicator
- One weapon: hitscan or fast projectile (lag comp is more interesting with hitscan)
- One map (grid of walls)
- Score/respawn — minimal
- That's it. Resist additions.

### Visual style
Developer-tool aesthetic. Flat colors, thin lines, monospace HUD text, visible grid. Dark background, cyan/magenta accents. The grid doubles as visual reference for the spatial-AOI system — make AOI radius visible as a circle. The goal is "engineering screenshot," not "game screenshot."

---

## Layer 3 — Measurement & Load Testing

This is built alongside the other layers, not at the end. It produces the numbers that go on the resume.

### Headless bot client
- Same protocol as real client (uses the same transport library)
- Scripted inputs (random walks, predictable patterns, optional adversarial behavior)
- Can spawn N at once via a controller
- Streams per-client metrics

### Artificial network layer
Lives in the transport library (Layer 0). Exposes runtime-configurable latency / jitter / loss / reorder. Connects to a sliders-UI for the interactive demo.

### Metrics (Prometheus)
- **Histograms:** server tick duration (target p99), packet RTT, snapshot serialization time
- **Counters:** bytes in/out per client, packets dropped, mispredictions, dropped inputs
- **Gauges:** connected players, AOI entity count per client, congestion-control state

### Grafana dashboard
- Panels grouped: Network / Simulation / Per-Client
- Coherent dark color scheme
- Right viz per metric (heatmaps for histograms, line graphs for rates, stat panels for counts)
- **Screenshot of this under 64-bot load is a centerpiece of the README.**

### Client debug HUD
Togglable overlay in the game client showing:
- RTT, packet loss %, jitter
- Predicted vs reconciled position drift (draw a ghost showing server's authoritative position)
- Interpolation buffer state
- Bytes/sec in and out
- Last N snapshot sizes
- AOI radius drawn in the world
- Congestion-control state

**This is where visual polish effort goes**, not into the game art.

### Network packet visualizer (optional, high-impact)
A small side window or web page showing packets flowing between client and server in real time — colored by channel, sized by payload, with timing. Makes the abstract visible.

---

## Numbers I want to produce (fill in after measuring)

- **Bandwidth:** naive baseline `__` KB/s/client → optimized `__` KB/s/client (`__`% reduction)
- **Tick performance:** `__` concurrent bots at `__` Hz, p99 tick duration `__` ms on `__` hardware
- **Prediction quality:** responsive up to `__` ms RTT, degrades past `__` ms
- **Reconciliation:** drift converges within `__` ms / `__` ticks under normal conditions
- **Reliability:** survives `__`% simulated packet loss with no application-visible disruption
- **Lag comp:** hit detection accurate within `__` ms of client-perceived state
- **Profiling win:** `[whatever I actually find and fix — leave blank until measured]`

---

## Suggested build order

### Month 1 — Foundation
1. **Week 1:** Project skeleton (CMake, dependencies, basic socket wrapper, threading scaffold)
2. **Week 2:** Transport library v1 — sequence numbers, ack bitmasks, single channel, connection handshake
3. **Week 3:** Multi-channel + reliability variants + fragmentation
4. **Week 4:** Artificial network layer + transport-library test suite + initial Prometheus metrics

### Month 2 — Game + sync
1. **Week 5:** Fixed-timestep server tick loop, naive "send full state every tick" snapshot protocol, minimal game (one moving circle)
2. **Week 6:** Headless bot harness, first Grafana dashboard, measure naive baseline bandwidth
3. **Week 7:** Delta compression + bit packing + quantization (measure each step)
4. **Week 8:** Interest management (spatial grid + AOI) — measure under dense load

### Month 3 — Prediction, polish, measurement
1. **Week 9:** Client prediction + server reconciliation (most important week)
2. **Week 10:** Lag compensation for hit detection + input validation + threat model doc
3. **Week 11:** Debug HUD, network visualizer, dashboard polish, interactive latency-slider demo
4. **Week 12:** Final measurement passes, profiling (find the bottleneck, fix it, write that bullet), README, architecture diagram, demo video

---

## Deliverables for the portfolio

1. **README.md** with:
   - Animated GIF / video at top showing the latency-slider prediction demo
   - One-line description, problem framing
   - Architecture diagram (real diagram, Excalidraw/tldraw — not ASCII)
   - Results table with measured numbers
   - "How to run it" that actually works
   - Threat model section
2. **Grafana dashboard screenshot** under load
3. **2–3 minute demo video** (unlisted YouTube): dashboard under load, latency slider showing prediction, side-by-side naive vs optimized bandwidth, packet visualizer, malicious-client rejection
4. **Threat model doc** in the repo
5. **Transport library as a separable artifact** (own folder, own README, own tests)

---

## Reading list (do before/during, not after)

1. **Glenn Fiedler's "Networking for Game Programmers"** (gafferongames.com) — read all of it. Sequence numbers, ack bitmasks, reliability, fragmentation, congestion control. This is the canonical reference for Layer 0.
2. **Valve's "Source Multiplayer Networking"** — clearest explanation of prediction, interpolation, and lag compensation. Maps directly to Layer 2.
3. **Quake 3 networking architecture** (Brian Hook writeup) — snapshot delta-compression model. Maps to Layer 1.
4. **yojimbo** and **GameNetworkingSockets** source — reference implementations of what I'm building. Look at their public APIs to understand the *shape* (not to copy implementation).

---

## How to work with me on this

- I'll come to you in chunks per the build order above. Don't try to design everything up front.
- When I'm implementing something, push me to **build the naive version first and measure** before optimizing.
- When I want to add a game feature, ask "does this serve the network-stack story?" If not, push back.
- For any optimization I attempt, ask for the before/after measurement.
- If you spot a place where I'm reinventing something I should pull from a library (e.g. Prometheus client), say so.
- If I drift toward making this look like Hamster v2, flag it.
