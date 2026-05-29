# Decisions

Short, append-only log of non-obvious architectural choices: the call, the alternative
rejected, and why. Newest at top. This doubles as interview prep.

## 2026-05-29 — Tick rate: 60 Hz
Authoritative sim runs at a fixed 60 Hz (`kDt = 1/60`).
- **Rejected:** 30 Hz.
- **Why:** 60 Hz halves input-to-state latency vs 30 and is the de-facto bar for action
  games. The fixed-timestep accumulator already decouples sim rate from loop/render rate,
  so the cost is CPU per tick — which we measure under bot load. Revisit only if P2 numbers
  say 60 doesn't scale.

## 2026-05-29 — Server I/O: non-blocking recvfrom drained per tick
The tick loop sets the socket `O_NONBLOCK` and drains ready datagrams each tick;
`EAGAIN`/`EWOULDBLOCK` returns `nullopt` and the loop advances the sim anyway.
- **Rejected:** blocking `recvfrom` (the skeleton default), and `epoll`.
- **Why:** the loop must tick on a fixed schedule regardless of packet arrival — blocking
  couples sim cadence to traffic, so a quiet client would stall the authoritative tick.
  `epoll` is the move when many clients make per-tick draining the bottleneck; that's a
  measured P2 decision, not a skeleton one.

## 2026-05-29 — Socket abstraction: concrete UdpSocket, no interface yet
The BSD socket lives behind a plain `UdpSocket` class (RAII over the fd); its public API
is the seam, not a polymorphic `ISocket`.
- **Rejected:** a pure-virtual socket interface up front so a second backend could slot in.
- **Why:** one backend exists. The first real consumer of indirection is the P1
  artificial-network shim — extract the interface when that forces the question, not before.
  An interface today is speculative; the class API is already the boundary callers see.

## 2026-05-27 — Dev/runtime platform: WSL2 + BSD sockets
Develop, build, and run inside WSL2 (Ubuntu) on Windows 11; BSD sockets only.
- **Rejected:** native Windows/Winsock (forces a portability shim immediately) and
  cross-platform-from-day-1 (premature abstraction — two socket backends before the loop
  even works once).
- **Why:** the project's value is defensible measured numbers on the *target* platform.
  Production game servers run on Linux; measuring on the Windows networking stack would
  undercut every bullet. WSL2 gives a real kernel, `epoll`, and `perf`/`bpftrace`.

## 2026-05-27 — Test framework: GoogleTest
- **Rejected:** Catch2.
- **Why:** standard for the property/wraparound tests the transport will need, strong
  FetchContent integration, familiar to reviewers.

## 2026-05-27 — Build order: walking-skeleton-first, not library-first
P0 runs input → fixed-tick server → state → client over a *raw* UDP socket before the
transport library exists; reliability is retrofitted onto a live spine in P1.
- **Rejected:** the spec's original month-by-month order (≈4 weeks of transport library
  before anything runs end-to-end).
- **Why:** always have a running system to measure against; avoid big-design-up-front.
