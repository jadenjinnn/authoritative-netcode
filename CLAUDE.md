# Authoritative Networked Game Server

## What & who
A real-time authoritative game server with a custom reliable-UDP networking stack
(client prediction, server reconciliation, lag-compensated hit detection), measured
under load with simulated network conditions. Portfolio project (#2, after the Hamster
2D engine) targeting systems-SWE roles. **The headline is the network stack; the game
is a deliberately minimal harness.** Full spec: `PROJECT_SPEC.md`.

Differentiator from Hamster: this is about the *wire*, not the engine. If work starts
looking like "Hamster v2" (ECS, rendering, editor for its own sake), that's a smell — flag it.

## Tech stack
- **Language/build:** C++17 / CMake
- **Dev + runtime platform:** WSL2 (Ubuntu) on Windows 11. BSD sockets only; Linux is
  the measurement target. The socket sits behind a thin interface, but only the BSD
  backend exists until there's a real reason for a second.
- **Tests:** GoogleTest (via FetchContent)
- **Metrics:** prometheus-cpp client → Prometheus → Grafana (stand up at P2)
- **Transport library** is a standalone artifact: own folder, own README, own tests.

## Conventions
- Small, reviewable diffs. One coherent change at a time.
- Naive-first, measure, then optimize. Every optimization needs a before/after number.
- Committed code reads as Jaden-authored: only the sparse, human-style comment a senior
  engineer would genuinely leave (a real invariant, a wraparound gotcha). No
  explain-every-line or AI-tell comments anywhere; strip scratch comments before commit.
  The "why" lives in design briefs + `docs/decisions.md` + narrated diffs, not in code.
- No AI tells in committed prose either (README is public — it must read as yours).
- Architectural decisions get a short entry in `docs/decisions.md`.
- (More accrue here as patterns settle — kept honest by `/architecture-update`.)

## Ways of working (this project)
- **Claude is the senior engineer.** States each architectural call with its reasoning
  and the rejected alternative; presents forks as "here's my call and why — poke holes,"
  not faits accomplis.
- **Jaden challenges decisions.** Pushback is expected and is the comprehension mechanism;
  the goal is that he can defend every decision in an interview.
- **One hand-written piece per phase.** Claude scaffolds the class/interfaces/tests/call
  sites and hands Jaden the core algorithm to implement himself, then reviews it.
  Earmarked pieces (confirm or swap at the start of each phase):
  - P0 fixed-timestep accumulator loop
  - P1 sequence-number + 32-bit ack-bitmask logic
  - P2 bot client input-driver state machine
  - P3 BitWriter/BitReader + quantization
  - P4 reconciliation rewind+replay (pair)
  - P5 lag-comp rewind-to-perceived-state hit test (pair)
  - P6 the profiling fix
- **Just-in-time reading.** Claude points at the specific Gaffer/Valve/Quake3 section when
  the relevant feature starts, not all up front.

## Autonomy (mirrors global, see ~/.claude/CLAUDE.md)
- Concise, no sycophancy. Brief reasoning for non-trivial choices.
- Multi-step work: propose plan → wait for approval → execute → report.
- Bugs found mid-task: log, don't inline-fix (per global bug rules).
- Feature work follows `~/.claude/feature-workflow.md`; bugs follow
  `~/.claude/bug-workflow.md`.
- Feature briefs carry why-this-approach + main tradeoff + the 1–2 tricky spots — but that
  reasoning goes in the brief and `docs/decisions.md`, NOT inline in code (this is the
  amended global rule: inline why-notes are scratch-only and never survive into commits).

## Phases (walking-skeleton-first; rough milestones, not specs)
- **P0 — Skeleton ("it's alive"):** input → fixed-tick authoritative server → full state
  → client, over a *raw* UDP socket. One circle moves. Prints a real RTT.
- **P1 — Transport library:** standalone reliable-UDP — seq/ack, handshake, mixed-reliability
  channels, fragmentation, congestion control, artificial-network shim, own test suite.
- **P2 — Measurement spine:** prometheus-cpp metrics + headless bot harness + first Grafana
  dashboard; capture naive full-state bandwidth baseline under N bots.
- **P3 — State-sync optimization:** delta compression → bit packing/quantization → interest
  management (AOI). Measure after each step.
- **P4 — Prediction & reconciliation:** client input prediction + rewind/replay; clock sync.
- **P5 — Lag comp & threat model:** server-side rewind hit detection + input validation +
  threat-model doc.
- **P6 — Demo & polish:** debug HUD, packet visualizer, latency-slider demo, dashboard
  polish, README + results table, profiling win.

Phases are deliberately rough. Each lands a runnable system and at least one measured number;
later phases layer one coherent capability onto the live spine. Resist front-loading.
