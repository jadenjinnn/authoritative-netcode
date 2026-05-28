# Decisions

Short, append-only log of non-obvious architectural choices: the call, the alternative
rejected, and why. Newest at top. This doubles as interview prep.

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
