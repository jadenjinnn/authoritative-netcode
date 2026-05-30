# Decisions

Short, append-only log of non-obvious architectural choices: the call, the alternative
rejected, and why. Newest at top. This doubles as interview prep.

## 2026-05-30 — Congestion control: Gaffer good/bad-mode rate governor (closes P1)
`RateGovernor` thresholds on the smoothed RTT from `RttEstimator`: a healthy link runs
in Good mode at a fast send interval (30 Hz), a congested one (RTT over ~250 ms) drops to
Bad and a slow interval (10 Hz). Dropping to Bad arms an adaptive penalty -- the link must
then stay healthy for that long before it earns its way back, and a relapse within the
good-dwell window doubles the penalty (capped at 60 s) so a flapping link can't thrash the
send rate. Pure policy on a caller-supplied microsecond clock, same shape as the estimator.
- **Rejected:** a bare single threshold (flaps mode -- and send rate -- whenever RTT dances
  around the setpoint); a fixed minimum hold time (doesn't adapt to a chronically flapping
  link); TCP-style AIMD (wrong curve for real-time, already rejected at the CC level);
  folding it into `RttEstimator`/`Connection` (breaks the measure/decide/bookkeep split).
  Also dropped the symmetric penalty *decay* (auto-halving on sustained good): it keys off
  the same dwell threshold as the doubling, entangling the two: penalty is monotone within a
  session and decay is a clean follow-up.
- **Why:** RTT climbs before loss does (a filling queue is latency before it's drops), so
  it's the early congestion signal. The adaptive penalty is the real mechanism -- it punishes
  flapping by demanding a longer clean stretch each relapse, which is what keeps a thrashing
  link from yo-yoing the send rate. This is *rate* control, not bounding the unacked set
  (that's flow control, still deferred). Built standalone and unit-tested; wiring it into a
  live send path waits for the `Peer` slice. Closes P1.

## 2026-05-30 — RTT estimation (foundation for congestion control); latency in the sim
Congestion control needs an RTT signal, and an RTT signal needs a path with real
latency. So before the control law: `SimSocket` grew `delay_us`/`jitter_us` (a
timestamped hold-queue flushed by `pump()`; `delay==0` bypasses it so loss-only
callers are unchanged), and a standalone `RttEstimator` samples RTT on the first ack
of each sequence and keeps an EWMA (Gaffer's 10% gain).
- **Rejected:** threading send/recv timestamps through `Connection` (it's a pure
  seq/ack bookkeeper -- keep timing out of it); reading a wall clock *inside*
  `SimSocket`/`RttEstimator` (non-deterministic, untestable). Both instead take
  caller-supplied microsecond times, so unit tests drive a fake clock.
- **Why:** the estimator stays decoupled and deterministic; the demo drives it from
  the ack window it already scans. Smoothing (EWMA) over raw samples is what makes
  the signal usable for a threshold -- jitter would otherwise flap any decision. This
  is CC-1; the good/bad-mode rate governor (CC-2) thresholds on `smoothed_us()` and
  closes P1.

## 2026-05-30 — App-layer fragmentation; fragments are not individually reliable
A serialized packet is split into <=1024-byte fragments, each its own UDP datagram
with a `FragmentHeader{packet_seq, fragment_id, fragment_count}`; the `Reassembler`
buffers a group until its last fragment arrives. A group orphaned by a lost fragment
ages out (bounded to 16 in-flight groups, oldest evicted) rather than completing.
- **Rejected:** letting IP fragment oversized datagrams (lose one IP fragment and the
  kernel drops the whole datagram, and many NATs/firewalls drop fragments outright);
  and making each fragment individually reliable (per-fragment acks/retransmit — far
  more state than resending the whole packet from the layer above).
- **Why:** doing it at the app layer controls the loss behavior and keeps every datagram
  sub-MTU. Fragments stay unreliable on purpose: a lost fragment loses the packet, and
  the reliable channel above just resends it. The `uint8` count caps a packet at ~260 KB,
  far above anything we send. Built standalone and unit-tested; wiring it into a unified
  packet send/recv path waits for the `Peer` abstraction (its own slice, shared with
  congestion control and the P2 harness) -- one coherent change at a time.

## 2026-05-30 — Mixed-reliability channels: a channel tag per message, uniform frame
One connection multiplexes an unreliable stream (snapshots/input — sent once, drop OK)
and the reliable stream (events) from slice 3. Each message carries a 1-byte channel tag;
a `ChannelMux` packs both senders into one packet and a `ChannelDemux` routes by tag on
receive (reliable deduped, unreliable passed through). Acks only touch the reliable channel.
- **Rejected:** per-channel grouped sections on the wire (`[channel][count]{msgs}*`), which
  saves the redundant id on unreliable messages but makes framing channel-aware and the
  parse loop branchier.
- **Why:** the uniform `[channel][id][len][bytes]` frame keeps serialization dumb and pushes
  all policy into the channel layer — same split as "`Connection` is a bookkeeper." The few
  wasted bytes on unreliable messages are P3's problem (bit-packing/quantization reclaims
  them properly); a slice shouldn't pre-optimize the wire. The two channels share only the
  packet sequence, so there's no cross-channel coupling to reason about.

## 2026-05-29 — Reliability layered on the ack system; resend the whole unacked set
Reliable delivery is a `ReliableChannel` *above* the packet ack system, not baked into
`Connection`. The sender keeps unacked messages queued and re-attaches the entire set to
every outgoing packet; an ack retires the messages that packet carried. Receiver dedups by
id (resends can arrive twice). Unordered for now.
- **Rejected:** folding message state into `Connection` (couples the pure seq/ack
  bookkeeper to delivery policy); per-message retransmit timers / selective NAK (more moving
  parts, needs an RTT estimate we don't have yet).
- **Why:** keeps `Connection` single-purpose and lets one connection host mixed reliabilities
  later (unreliable + reliable channels). Re-sending the full unacked set is the
  naive-correct baseline — loss self-heals with no loss detection at all. Bounding what
  rides each packet is congestion control (slice 5); unbounded is fine until a bandwidth
  number says otherwise. An ordered channel later just layers a reorder buffer on receive.

## 2026-05-29 — Socket interface (`ISocket`) extracted for the artificial-network shim
The send/recv seam is now a pure-virtual `ISocket` (`send_to`, `try_recv_from`,
`local_endpoint`); `UdpSocket` and the `SimSocket` loss shim both implement it. Resolves
the "extract when forced" note in the concrete-`UdpSocket` entry below.
- **Rejected:** an always-on pass-through shim (impairment off in prod) — puts indirection
  on the latency path we measure; and templating callers on socket type — bloats call sites
  for a vcall that's noise next to `sendto`.
- **Why:** the shim is the first real second implementation, so the seam is no longer
  speculative. Runtime polymorphism lets measurement builds wrap a real socket while
  production wires `UdpSocket` directly (no shim overhead on the measured path); the
  vcall (~ns) is negligible against the `sendto` syscall (~µs).

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
