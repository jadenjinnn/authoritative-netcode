# Feature spec: Peer abstraction

> Tier: **Design**
> Status: **shipped**
> Started: 2026-05-31
> Spec author: Jaden Jin

---

## Classification

- **Reversibility**: Reversible
- **Scope**: Cross-cutting
- **Tier rationale (1 sentence)**: Additive composition over five existing modules with no consumer at ship time, so it's cheap to revert — but it spans the whole transport library, which rules out Sketch.

---

## Problem

The transport library has all the reliable-UDP pieces — `Connection` (seq/ack), `ChannelMux`/`ChannelDemux` (mixed reliability), `fragment`/`Reassembler`, `RttEstimator`, `RateGovernor` — but nothing composes them into a single send/receive path. Every demo re-implements the same glue by hand: a copy-pasted `write_packet`/`read_packet` codec, the same `for i in 0..32` ack-retire scan, and an ad-hoc send loop with no rate limiting. The `RateGovernor` built to close P1 has no consumer at all — it computes a send interval that nothing reads. P2 needs one connection object (`serialize → fragment → send` / `recv → reassemble → dispatch`) so the bot harness and the bandwidth baseline have a real send path to measure, and so fragmentation and the governor finally sit on that path instead of in isolated demos.

## In scope

- A `Peer` class: one instance = one connection to one remote endpoint, over an injected `net::ISocket&`.
- Send path: `queue_reliable` / `queue_unreliable` enqueue messages; `update(now_us)` flushes a single packet when the governor's send interval has elapsed — `pack (mux) → serialize (header + messages) → fragment → send_to` for each datagram.
- Receive path: `poll()` drains the socket, reassembles fragments, parses packets, feeds the `Connection` ack state + `RttEstimator`, retires acked reliable messages, and returns a `Delivery{reliable, unreliable}` batch (pull model).
- Self-fed RTT + congestion: Peer records send times into `RttEstimator` on flush, samples RTT on ack, and calls `RateGovernor::update` so the send interval reacts to measured RTT.
- Injected `Clock` (the `net::Clock` typedef) so pacing/RTT are deterministic under test.
- Egress counters: packets, fragments, and bytes sent (read by the P2 metrics slice).
- A `peer_demo` exercising two Peers over a `SimSocket` (loss + latency), and unit tests.

## Out of scope

- **Server-side fan-out** (a `map<Endpoint, Peer>` / PeerManager). One Peer talks to one remote; multi-client routing lands with the bot harness.
- **Migrating `server/` and `client/` apps onto Peer.** They stay on the raw-`Snapshot` socket path this slice; the bot harness is the first real consumer.
- **A shared/extracted wire codec.** Peer owns a private memcpy codec; the six demos keep their local copies. P3's BitWriter/BitReader unifies and replaces all of them — extracting one now is work P3 throws away.
- **Connection handshake / teardown.** Peer assumes the remote endpoint is known up front (passed at construction). No connect/disconnect state machine.
- **Ordered reliable delivery, byte-order/endianness handling, bounding the unacked set, `seen_` set growth.** All pre-existing deferrals, untouched here (P3 and later).
- **Pumping the SimSocket.** `pump()` is not on `ISocket`, so the caller's loop owns it; Peer cannot and does not call it.

## API sketch

```cpp
// One connection to one remote, over any ISocket (real or sim).
net::UdpSocket udp;            udp.bind(0); udp.set_nonblocking();
net::SimSocket  wire(udp, {0.1 /*loss*/, 42, 30'000 /*delay us*/});

reliable::Peer peer(wire, server_addr, clock);   // clock: () -> uint64_t microseconds

// --- send side: queue freely, Peer paces ---
peer.queue_unreliable(snapshot_bytes);   // dropped-is-fine (snapshots/input)
peer.queue_reliable(event_bytes);        // resent until acked
peer.update(now_us());                    // flushes ONE packet iff send interval elapsed
wire.pump();                              // caller drives the sim's latency queue

// --- recv side: pull a demuxed batch ---
reliable::Peer::Delivery in = peer.poll();
for (const reliable::Message& m : in.unreliable) { /* latest snapshot */ }
for (const reliable::Message& m : in.reliable)   { /* each event once */ }

// --- metrics read these ---
peer.bytes_sent(); peer.packets_sent(); peer.fragments_sent();
peer.rtt().smoothed_us(); peer.mode();   // governor mode for dashboards
```

---

## Design

### Data structures

New file pair `transport/reliable/peer.{h,cpp}`, namespace `reliable`. `Peer` owns (by value) the pieces it composes:

```cpp
class Peer {
public:
    using Delivery = ChannelDemux::Delivery;   // {vector<Message> reliable, unreliable}

    Peer(net::ISocket& socket, net::Endpoint remote, net::Clock clock);

    void queue_reliable(std::vector<uint8_t> payload);
    void queue_unreliable(std::vector<uint8_t> payload);

    // Flush at most one packet, iff governor's interval has elapsed since last flush.
    // Returns true if a packet went out.
    bool update(uint64_t now_us);

    // Drain socket -> reassemble -> parse -> ack/RTT bookkeeping -> demux. Pull model.
    Delivery poll();

    uint64_t bytes_sent()     const;
    uint64_t packets_sent()   const;
    uint64_t fragments_sent() const;
    const RttEstimator& rtt() const;
    RateGovernor::Mode  mode() const;

private:
    net::ISocket& socket_;
    net::Endpoint remote_;
    net::Clock    clock_;

    Connection    connection_;     // seq/ack
    ChannelMux    mux_;            // send-side reliable + unreliable
    ChannelDemux  demux_;         // recv-side routing + dedupe
    Reassembler   reassembler_;   // recv-side fragment join
    RttEstimator  rtt_;
    RateGovernor  governor_;

    uint64_t last_flush_us_ = 0;
    bool     flushed_once_  = false;

    uint64_t bytes_sent_ = 0, packets_sent_ = 0, fragments_sent_ = 0;
    std::array<uint8_t, 8192> rx_buf_{};   // recv scratch
};
```

Wire layout (Peer-private codec, memcpy-based, matches the demos so it's already battle-tested):
`[PacketHeader][uint16 msg_count]{ [uint8 channel][uint16 id][uint16 len][len bytes] }*`
This serialized blob is then handed to `fragment(packet_seq, blob, len)` → one or more datagrams, each `[FragmentHeader][chunk]`.

### Module touchpoints

- **New:** `transport/reliable/peer.{h,cpp}`.
- **`transport/reliable/CMakeLists.txt`** — add `peer.cpp` to the `reliable` library target.
- **`transport/examples/CMakeLists.txt`** — add `peer_demo`.
- **`transport/examples/peer_demo.cpp`** — new.
- **`tests/CMakeLists.txt`** + **`tests/peer_test.cpp`** — new test target linked against `reliable`.
- **No edits** to `connection.{h,cpp}`, `channel.{h,cpp}`, `fragment.{h,cpp}`, `rtt.{h,cpp}`, `rate_governor.{h,cpp}`, `socket.h`, `sim_socket.{h,cpp}`. Peer consumes their existing public APIs unchanged. (If a needed accessor turns out missing — e.g. a const getter — that's a spec amendment, not a silent edit.)

### Lifecycle / control flow

Per caller loop iteration:

1. `peer.poll()` — for each datagram from `socket_.try_recv_from`: feed bytes to `reassembler_.reassemble`; on a completed packet, parse header+messages, `connection_.on_received(header)`, then scan the ack window (`for i in 0..32`: `connection_.is_acked(ack - i)` → `mux_.on_acked(seq)` **and** `rtt_.on_acked(seq, now)`), then `demux_.route(messages)` and accumulate into the returned `Delivery`. The ack-retire scan that's copy-pasted across demos lives here, once.
2. Caller handles the returned messages (e.g. bot updates its view of the world).
3. `peer.queue_*` — caller enqueues this tick's outgoing messages.
4. `peer.update(now_us)` — if `!flushed_once_` or `now_us - last_flush_us_ >= governor_.send_interval_us()`: `header = connection_.next_header()`; `msgs = mux_.pack(header.sequence)`; serialize; `rtt_.on_sent(header.sequence, now_us)`; `fragment()`; `send_to` each datagram; bump counters; `last_flush_us_ = now_us`; `flushed_once_ = true`. Then `governor_.update(rtt_.smoothed_us(), now_us)` so the next interval reflects current RTT.
5. Caller calls the socket-specific `pump()` if it's a SimSocket with latency.

Construction wires the references; no separate init. Destruction is trivial (all members owned by value / reference; no sockets owned).

### Edge cases

- **`poll()` before any send** — `Connection`/`RttEstimator` handle never-sent sequences (return false / ignored); empty `Delivery` returned. Fine.
- **`update()` called every microsecond** — gated by the interval; only flushes on schedule. First call always flushes (`flushed_once_` false) to seed the connection.
- **Empty flush** — if nothing is queued, a flush still sends a header-only packet (count 0). That's intended: it carries acks back and gives RTT/governor a heartbeat, exactly like the demos' empty-ack packets. (Could suppress later if bandwidth says so — noted as future work, not now.)
- **Fragment of a lost group** — `Reassembler` ages out incomplete groups (bounded 16); the whole packet is simply not delivered, and reliable messages on it ride the next flush. No Peer-level handling needed.
- **Oversized packet** — `fragment()` already splits up to 255 fragments (~260 KB). A packet beyond that is a logic error upstream, not a runtime case P2 hits (snapshots are tens of bytes). Not guarded.
- **Duplicate/reordered datagrams** — `Connection` ack state and `ChannelDemux` dedupe are wraparound- and resend-safe already; Peer adds nothing.
- **Recv buffer too small** — `rx_buf_` is 8 KB, well above a single ~1 KB fragment datagram; `try_recv_from(buf, cap)` truncation isn't expected. If a datagram exceeds cap it's dropped like loss (documented assumption).
- **Threading** — Peer is single-threaded, not reentrant. One Peer is driven by one loop. (Per-client threading is a deferred P2 open question.)

---

## Why this approach

Three calls drove the design, each with a rejected alternative:

1. **Peer owns pacing (vs. caller paces).** The entire reason to build Peer is to stop copy-pasting the send loop across demos and the coming harness, and to give `RateGovernor` — finished but unconsumed — something to drive. If the caller paced, every consumer would re-implement the interval check and ack scan, which is the duplication this slice exists to kill. Cost accepted: Peer needs an injected clock and becomes "smart" rather than pure mechanism.

2. **Pull dispatch (vs. push callbacks).** `poll()` returning a `Delivery` reuses `ChannelDemux::Delivery` verbatim — a type already built and tested — and is trivial to assert on in unit tests. Push callbacks add `std::function` indirection and capture-lifetime footguns for zero benefit in a headless, single-threaded harness. Rejected as premature.

3. **Peer + demo + tests only (vs. migrate server/client now).** Keeps the diff to one new class with its own test, per the small-reviewable-diffs rule. Nothing is measured until the bot harness exists, so leaving the apps on the raw path costs nothing now; migrating them couples two concerns in one diff before the consumer exists. The server migration becomes its own slice when the harness needs it.

Composition over inheritance throughout: Peer holds the pieces by value and calls their public APIs, touching none of their internals. That's what keeps it Reversible — delete the file pair and the stack is exactly as it was. The private memcpy codec deliberately duplicates the demos' codec one last time rather than extracting a shared one, because P3's BitWriter rewrites all of it; extracting now is throwaway work.

## Risks / what could go wrong

- **`fragment()` keys groups on the 16-bit packet sequence; `Reassembler` tracks only 16 groups.** At 30 Hz a sequence wraps every ~36 min and 16 groups is ~0.5 s of packets — fine. But if a future caller drives `update()` far faster than it `poll()`s the peer (asymmetric loops), in-flight fragment groups could evict before completing and silently drop packets that *weren't* lost on the wire. Mitigation: the demo and tests drive poll/update symmetrically; flag if the harness diverges.
- **Empty heartbeat flushes cost bandwidth.** Flushing a header-only packet every interval even with nothing queued adds a steady ~12-byte-packet floor. Harmless at P2 scale, but it lands in the very bandwidth baseline P2 measures — note it when reading the number so the baseline isn't misread as payload.
- **RTT only samples when acks flow back.** Acks ride packet headers (Connection acks every received sequence), so RTT stays fresh as long as `update()` keeps flushing — including empty heartbeats. If heartbeats are ever suppressed without thought, a quiet connection's RTT goes stale and the governor coasts on its last estimate. Not a bug now; a trap for the future heartbeat-suppression work.
- **Governor updated post-flush reads prior-ack RTT.** The `on_sent` for the just-flushed packet can't have an RTT yet; `governor_.update` reads `smoothed_us()` reflecting *earlier* acks. That's correct (pace on observed RTT, not the in-flight packet), but it's a subtle ordering a careless refactor could invert — call it out in review.
- **Clock injection mismatch.** `net::Clock` is `std::function<uint64_t()>`; `update(now_us)` also takes an explicit `now_us`. Two time sources risk drift if a caller passes a `now_us` inconsistent with what the `clock_` SimSocket sees. Mitigation: Peer uses the passed `now_us` for pacing/RTT and avoids calling `clock_` itself; document that callers pass one coherent clock. Resolve in the first cut: possibly drop the stored `clock_` entirely if `now_us` covers every need.

## Success criteria

- `peer_test` passes, covering: (a) two Peers over a lossless `SimSocket` deliver all reliable messages and dedupe resends to exactly-once; (b) under ~30% loss, reliable messages still reach 100% while unreliable thin out; (c) `update()` respects the send interval — N calls within one interval produce exactly one flushed packet; (d) a multi-fragment packet (payload > `kMaxFragmentSize`) round-trips intact through fragment→reassemble inside Peer; (e) `bytes_sent`/`packets_sent`/`fragments_sent` match a hand-computed expectation for a known message set.
- `peer_demo` runs to completion over a lossy+latent `SimSocket` and prints: reliable delivery %, unreliable delivery %, retransmits, smoothed RTT, governor mode flips, and total bytes/packets/fragments sent — a single run that exercises the whole composed path.
- Full suite stays green (34 existing tests + new `peer_test`); clean build, no warnings, no edits to the composed modules' sources.

## Test extensions required

- New `tests/peer_test.cpp` (new `peer_tests` target in `tests/CMakeLists.txt`) covering the five cases above. This is the slice's primary coverage — Peer is the first place fragmentation, channels, acks, RTT, and the governor run *together*, so the test asserts the composition, not the pieces (those keep their own tests).
- `peer_demo` is a runnable example, not a test, but it doubles as an integration smoke check and banks the P2-relevant numbers (bytes/packet, RTT under impairment) for the README results table.
- No changes to existing tests — the composed modules are untouched, so their suites stay valid as-is.

---

## Decisions during implementation

<!-- Append-only. Dated entries for non-obvious choices made while building. -->

## Spec amendments

### 2026-05-31 — `connection.{h,cpp}` moved into scope to fix a latent ack-sentinel bug
The spec listed `connection.{h,cpp}` as out of scope ("no edits to composed modules"). Building Peer surfaced a pre-existing bug it can't avoid: Peer flushes unconditional heartbeat packets, so a peer that has received nothing still sends `ack = 0` (its zero-initialized `remote_sequence_`). The recipient couldn't distinguish that default from a genuine acknowledgement of packet sequence 0, so a first packet dropped on the wire was falsely retired and never resent — one reliable message lost (`Peer.UnderLossReliableCompletesUnreliableThins`, payload id 0, seed-dependent). The old demos never hit it because their servers only acked in response to a received packet. Fix (approved by Jaden): reserve sequence 0 as a null sentinel — real sequences start at 1 and skip 0 on wraparound, and `process_acks` ignores `ack == 0`. Three-line change to `connection.{h,cpp}` plus a `Connection`-level regression test (`DefaultAckFromSilentPeerDoesNotFalselyAck`). The Module touchpoints "no edits" line is superseded for these two files.

---

## Future work (out-of-scope ideas surfaced during this feature)

- Suppress empty heartbeat flushes (or send a tiny keepalive on a slower cadence) once a bandwidth number justifies it.
- Server-side `PeerManager` (`map<Endpoint, Peer>`) for multi-client fan-out — lands with the bot harness.
- Migrate `server/`/`client/` apps off the raw-`Snapshot` path onto Peer.
- Collapse the per-flush ack-window scan and the demos' duplicated codec into P3's BitWriter/BitReader.
