# Decisions

Short, append-only log of non-obvious architectural choices: the call, the alternative
rejected, and why. Newest at top. This doubles as interview prep.

## 2026-06-07 — AOI / interest management + density-bounded measurement (P3 slice 3, closes P3)
Each client receives only entities within radius R=20 of its own entity. The slice-1 delta is
reused unchanged: filter BOTH the current and baseline snapshots (each centered on the viewer's
position AT that tick) and diff them — entered-AOI = add, left-AOI = remove. No per-client state is
stored; the recomputed baseline view matches what the client holds because filtering is
deterministic (same R, same center from history). Brute-force O(N) query per client.
- To honestly show the asymptotic win, the world bound + quantize range were made runtime
  (`quantize(v, max)`) and bots spawn uniformly, enabling a density-controlled sweep.
- **Rejected:** storing each client's last-sent set (extra state for an unmeasured CPU saving);
  AOI keyframes without delta; a grid index (premature — brute force holds 60 Hz at N=200; grid is
  a P6 win with a before/after number); fixed-world-only measurement (would hide the density caveat).
- **The key insight (interview):** AOI is NOT a free asymptotic win. In a fixed world, growing N
  raises density, so k ∝ N and total egress stays O(N²) — AOI is just a ~(AOI area / world area)
  constant factor. The O(N·k) curve-bend appears only under BOUNDED density (world grows with N).
- **Measured.** Fixed world (constant-factor): N=100 egress 3088 -> ~565 KB/s (~5.5x vs slice 2),
  avg-k ~14. Density-bounded sweep (R=20, k≈6 held constant): per-client egress FLAT at ~3.3 KB/s
  across N=50/100/200 (bound 100/141/200) while total scaled linearly (160/327/668 KB/s) — the
  O(N·k) signature; slice-2 per-client doubled over the same range. 60 Hz held at N=200.

## 2026-06-07 — Bit-packing + quantization for state sync (P3 slice 2)
Per-entity wire encoding goes from 12 bytes (u32 id + 2x f32) to 40 bits (16-bit id + two 12-bit
quantized positions) via a new `BitWriter`/`BitReader` and `quantize`/`dequantize` over [0, 100]
(World's clamp range). The leading snapshot type byte stays raw for dispatch; the rest of the
payload is one continuous bitstream (header fields at full width, so no byte-alignment juggling
between headers and the bit-packed body).
- **Rejected:** 16-bit positions (half the savings for imperceptible extra precision); field-level
  / adaptive precision and gap-coded ids (premature; deferred); packing the headers too (marginal).
- **Why:** quantize + pack is the constant-factor lever that helps even at 100% churn (it shrinks
  every field on the wire), unlike delta. 12 bits gives a 0.024-unit step — finer than a tick's
  0.33-unit move — and more than halves the per-entity cost.
- **Gotchas (interview):** quantize divisor is `2^bits - 1`, not `2^bits` (max maps to the top
  level, no overflow); round, don't truncate; the bitstream is MSB-first and the reader must mirror
  the writer; reads past the end latch `ok() = false` (how `apply_snapshot` now rejects truncation).
- **Measured:** N=50 1837 -> 806 KB/s (-56%), N=100 7201 -> 3088 KB/s (-57%), 60 Hz held, 100%
  delta under load. A touch under the 58% per-entity figure because packet + Peer transport headers
  don't shrink. This is the win delta couldn't deliver at full churn.
- **Hand-write:** Jaden wrote `BitWriter`/`BitReader`; `quantize`/`dequantize` delegated to Claude.

## 2026-06-07 — Delta compression: per-client baseline via app-level snapshot ack (P3 slice 1)
The server sends each client a delta of the current tick against the last snapshot that client
confirmed, falling back to a full keyframe when it has no baseline (new join) or its baseline aged
out. The ack is app-level: the client stamps `last_received_tick` into its input packets; the
server keeps a fixed-depth ring (`SnapshotHistory`, 128 ticks) to diff against. Snapshots are
type-tagged (keyframe vs delta) on the wire; deltas omit unchanged entities (absence = carry
forward) and list removed ids.
- **Rejected:** piggybacking the transport's packet ack to learn the baseline — it inverts
  layering (transport reaching into sim concepts) and acks "datagram arrived," not "snapshot
  applied," which diverge under fragmentation. Also rejected field-level changed-masks (buys
  nothing when both position fields move every tick; deferred to the bit-packing slice) and a
  reliable snapshot channel (stale snapshots are worthless — re-seed with a keyframe instead).
- **Why:** app-level ack keeps the dependency one-way (sim -> transport) and acks the event we
  care about; it's the Quake3/Source model. Entity-level granularity is naive-first.
- **Measured finding (the point of the slice):** at 100% positional churn (all-moving bots) a
  delta is ~0.5-1% *larger* than a keyframe (extra `baseline_tick` + `removed_count` headers, with
  every entity sent anyway). N=50: 1837 KB/s delta vs 1819 keyframe (P2 baseline ~1.8 MB/s). N=100:
  7201 KB/s delta vs 7166 keyframe (P2 ~7.0 MB/s). Tick rate held 60 Hz at N=100 despite per-client
  serialization. Conclusion: delta's payoff scales with the *idle* fraction, which this synthetic
  load has none of — the real wins come next from quantization (constant factor, helps even at full
  churn) and AOI (asymptotic). The machinery is the deliverable; reconciliation (P4) and AOI reuse it.
- **Cleanup:** removed the now-superseded untagged `encode_snapshot`/`decode_snapshot` rather than
  leaving a second snapshot format behind (see the spec's implementation-decisions log).

## 2026-06-01 — Metrics export + observability stack (P2 slice 3, closes P2)
The server exports the counters it already computes via prometheus-cpp's `Exposer` (civetweb
on its own thread serving `/metrics:8080`); the game loop only bumps lock-free metric objects.
Metrics: `egress_bytes/packets/reliable_events_total` (counters) + `connected_clients`,
`entities`, `tick_rate_hz` (gauges). Rates are computed in the dashboard via PromQL `rate()`,
not server-side. Prometheus + Grafana run in Docker Compose (observability only); the measured
server + bots stay native on the WSL host.
- **Rejected:** a hand-rolled `/metrics` (re-deriving the exposition format + a non-blocking
  accept loop is yak-shaving with no learning payoff; "integrated the standard Prometheus
  client" is the portfolio signal); pre-computed bytes/s gauges (bakes in our 1 s window,
  loses info, can't re-aggregate — counters + `rate()` is idiomatic and survives restarts);
  containerizing the server/bots (Docker's NAT hop would taint the latency/bandwidth numbers,
  same reason k8s was rejected); per-client metric labels (cardinality blowup at N=100 with
  churn, for zero P2 value — `total / clients` answers per-client cost in the dashboard).
- **Why:** closes the P2 measurement spine — the baseline is now a live graph, not a one-shot
  stdout read, giving P3 a before/after instrument. Verified end-to-end: target UP via
  `host.docker.internal`, dashboard renders, numbers track the O(N²) curve. The Exposer's
  background thread is the project's one genuinely concurrent piece; prometheus-cpp counters
  are atomic, so the loop writes while civetweb reads with no lock. stdout printf kept as a
  dependency-free fallback.

## 2026-05-31 — Multi-client server + bot harness; the naive full-state baseline (P2 slice 2)
The server became a real authoritative multi-client server: a `PeerManager` owns one
`Peer` per remote, `World` holds one entity per client, clients send unreliable `Input`
(applied to their entity) plus occasional reliable events, and the server broadcasts the
full state to everyone each tick. A headless `bots/` swarm drives N bots (each a `Peer`)
to put it under load and read the egress baseline.
- **Each bot is an entity (not a pure traffic generator):** only this makes the broadcast
  scale with player count, so the baseline is the deliberately-bad O(N²) curve that
  motivates P3. Measured (localhost, 60 Hz): N=1 → 1.8 KB/s, N=50 → 1.8 MB/s, N=100 →
  7.0 MB/s. Per-client egress doubles 50→100 (36→72 KB/s) while clients double → ~4× total.
  That quadratic is the P2 deliverable; P3 (delta/quantization/AOI) beats it.
- **`PeerManager` routes by source into per-Peer inboxes:** a `Peer` drains its own socket
  and assumes one remote, so it can't share the server socket directly. PeerManager owns
  the real socket, drains once, and feeds each datagram to the owning Peer's private
  in-memory inbox `ISocket`; sends pass through. Peer stays unmodified. Rejected: teaching
  Peer to be multi-remote (conflates one connection with a registry of them).
- **Both channels driven:** authority is the input→sim→state loop (inputs ride *unreliable*
  — a dropped input is obsolete before a resend arrives). But the harness is Peer's first
  real multi-client consumer, so bots also send occasional *reliable* events to exercise
  the reliable channel + mux/demux on a live path. Headline number stays downstream egress.
- **Stdout for the number, Prometheus next slice; `client/` left as a P0 relic** (its raw
  single-`Snapshot` reader no longer parses the multi-entity Peer protocol — accepted, the
  bots are the real client now).

## 2026-05-31 — Sequence 0 reserved as a null ack sentinel
Real packet sequences start at 1 and skip 0 on wraparound; `Connection::process_acks`
ignores `ack == 0`. Surfaced by `Peer`: it sends unconditional heartbeat packets, so a
peer that has received nothing still advertises its zero-initialized `remote_sequence_`
(0) as the ack — indistinguishable from a genuine acknowledgement of packet 0. The
result was a dropped first packet being falsely retired and never resent (one reliable
message lost, seed-dependent).
- **Rejected:** a connection handshake that establishes sequence baselines before data
  flows (the textbook fix, but out of scope until a later phase); a per-packet "have I
  sent anything?" flag threaded into the ack scan at the `Peer` level (pushes a
  `Connection` invariant up into its caller, where every future consumer would have to
  re-derive it).
- **Why:** reserving one sequence value makes the zero-default unambiguous at the layer
  that owns sequences, so every consumer (Peer, the demos, the future server) inherits the
  fix for free. The cost is one unusable sequence per ~65k — negligible. The older demos
  never hit this because their servers only acked in response to a received packet.

## 2026-05-31 — Peer: one object composes the transport (P2 slice 1)
`Peer` (one instance per remote) owns the `Connection`, `ChannelMux`/`ChannelDemux`,
`Reassembler`, `RttEstimator`, and `RateGovernor` by value, behind `queue_reliable` /
`queue_unreliable` / `update` / `poll`. `update(now_us)` paces sends on the governor's
interval (its first real consumer) and runs the flush pipeline (pack → serialize →
fragment → send); `poll(now_us)` reassembles, applies acks, samples RTT, and returns a
demuxed batch. Times are caller-supplied microseconds, as everywhere else in the layer.
- **Rejected:** caller-driven pacing with `Peer` as pure mechanism (every consumer —
  demos, bot harness, server — re-implements the interval check and the 0..32 ack-retire
  scan, the exact duplication this slice removes); push-style dispatch via per-channel
  `std::function` callbacks (capture-lifetime footguns, indirection, no benefit in a
  single-threaded headless harness); migrating the server/client onto Peer in this slice
  (couples two concerns before a consumer exists). Also kept the composed modules
  unedited (composition, not inheritance) so the slice stays revertible by deleting one
  file pair.
- **Why:** the point of `Peer` is to stop hand-rolling the send/recv glue and to give the
  finished-but-unconsumed `RateGovernor` something to drive. `poll()` returns the existing
  `ChannelDemux::Delivery` verbatim, so it's trivial to assert on. The one subtlety worth
  defending: `update` feeds the governor `rtt_.smoothed_us()` *after* sending, but that
  reflects prior acks (the just-sent packet has no round-trip yet) — you pace on observed
  RTT, not an in-flight guess.

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
