# Feature spec: P3 slice 2 — bit-packing + quantization for state sync

> Tier: **Design**
> Status: **shipped**
> Started: 2026-06-07
> Spec author: Jaden Jin (drafted by Claude, pending approval)

---

## Classification

- **Reversibility**: Reversible
- **Scope**: Isolated
- **Tier rationale (1 sentence)**: Matrix says Sketch (Reversible × Isolated), overridden to Design because the hand-written `BitWriter`/`BitReader` + quantization carry real correctness edge-cases (bit order, final-byte padding, exact round-trip, quantize rounding/clamping at the range boundaries) worth pinning before the core is written.

---

## Problem

After slice 1, each entity on the wire is still 12 bytes — `id` (u32) + `x`,`y` (f32) — and at 100% churn delta buys ~nothing, so egress tracks the naive baseline (N=100 ~7.2 MB/s). Most of those bits are waste: ids run 1..N (≤ ~7 bits at N=100, sent as 32), and a float32 carries ~7 digits of precision over a world that is only 0–100 units wide where entities move ~0.33 units/tick. This slice spends only the bits the data needs — quantize positions to a fixed grid and pack fields at bit granularity — for a **constant-factor win that holds even at full churn** (it shrinks every field sent, churn or not), unlike delta.

## In scope

- A `BitWriter`/`BitReader` pair (write/read arbitrary bit-width values, byte-padded at the end).
- Quantize/dequantize for a scalar over `[kPosMin, kPosMax]` at `kPosBits` precision.
- Rewrite the per-entity encoding inside `encode_keyframe` / `encode_delta` / `apply_snapshot` to bit-pack `id`(16) + `qx`(12) + `qy`(12) = 40 bits/entity; removed ids packed at 16 bits.
- Keep per-packet header fields (type, tick, baseline_tick, counts) at full width — not shrunk (per Fork 3) — but routed through the same bitstream so the payload is one seamless stream (no alignment juggling).
- Update existing snapshot tests' position assertions from exact equality to within-quantization-tolerance.
- Re-measure egress at N=50 / N=100 against the slice-1 numbers.

## Out of scope

- Shrinking header fields (tick/counts) — full width; marginal saving, more code.
- Delta-encoding ids (gap coding) — fixed 16-bit; revisit if a measurement wants it (Future work).
- Variable/adaptive precision, per-field precision, velocity/other fields — positions only.
- AOI / interest management — the next P3 slice; where the asymptotic win lives.
- Any change to `World`, the server loop, the bot client, or metrics — `encode_*`/`apply_snapshot` signatures are unchanged, so callers don't move.
- Endianness portability of the bitstream across machines of different byte order — single-arch (localhost / same-arch deploy) for now.

## API sketch

C++ only.

```cpp
// --- New: sim/quantize.h ---
constexpr float   kPosMin  = 0.0f;
constexpr float   kPosMax  = 100.0f;   // must cover World's clamp range (world.cpp kBound)
constexpr int     kPosBits = 12;       // 4096 levels; step = (max-min)/(2^bits - 1) ~= 0.024
uint32_t quantize(float v);            // clamp to range, map to [0, 2^kPosBits - 1]
float    dequantize(uint32_t q);       // map back to [kPosMin, kPosMax]

// --- New: sim/bitstream.h ---
class BitWriter {
public:
    void write_bits(uint32_t value, int bits);   // writes the low `bits` bits, MSB-first
    std::vector<uint8_t> take();                  // pad final byte with 0s, return buffer
};
class BitReader {
public:
    BitReader(const uint8_t* data, size_t len);
    uint32_t read_bits(int bits);                 // next `bits`, MSB-first
    bool ok() const;                              // false once a read ran past the end
};

// --- Per-entity encoding inside the snapshot codec ---
std::vector<uint8_t> out;
out.push_back(static_cast<uint8_t>(SnapshotType::Keyframe));  // raw lead byte for dispatch
BitWriter w;
w.write_bits(tick, 32);
w.write_bits(count, 16);
for (const EntityState& e : snap.entities) {  // per entity = 40 bits
    w.write_bits(e.id, 16);
    w.write_bits(quantize(e.x), kPosBits);
    w.write_bits(quantize(e.y), kPosBits);
}
std::vector<uint8_t> bits = w.take();
out.insert(out.end(), bits.begin(), bits.end());
```

Wire layout (the leading type byte stays raw so `apply_snapshot` can dispatch without a reader; everything after is one bitstream):

```
keyframe: [u8 type=0] | bits{ tick:32, count:16, (id:16, qx:12, qy:12) x count }            (padded)
delta:    [u8 type=1] | bits{ tick:32, baseline_tick:32,
                              changed:16, (id:16, qx:12, qy:12) x changed,
                              removed:16, (id:16) x removed }                                 (padded)
```

---

## Design

### Data structures

- `sim/quantize.h` — the three constants above + `quantize`/`dequantize` free functions. Range is declared here (the protocol owns its assumption), documented to match `World`'s clamp bound.
- `sim/bitstream.h/.cpp` — `BitWriter` (a byte vector + a small bit accumulator) and `BitReader` (data ptr + len + bit cursor + an `ok_` flag). MSB-first within each byte. `BitWriter::take()` flushes the partial final byte (zero-padded). `BitReader::read_bits` past the end sets `ok_ = false` and returns 0.
- No change to `WorldSnapshot` / `EntityState` / the `SnapshotType` tag.

### Hand-write split (this phase's reserved piece — Jaden)

- **Jaden writes the bodies of:** `BitWriter::write_bits`, `BitWriter::take`, `BitReader::read_bits` (+ `ok`), `quantize`, `dequantize`.
- **Claude scaffolds:** the `bitstream.h` / `quantize.h` declarations + doc comments + constants; the test files (cases that pass once the bodies are correct); the `snapshot.cpp` call-site rewrite that uses the new tools; CMake; and the tolerance updates to existing tests.
- Sequencing (implement phase): scaffold headers + stubs + failing tests → Jaden fills the bodies → bitstream/quantize tests green → wire the codec → snapshot tests green.

### Module touchpoints

- `sim/quantize.h` (new), `sim/bitstream.h/.cpp` (new).
- `sim/snapshot.cpp` — per-entity encode/decode in `encode_keyframe`, `encode_delta`, `apply_snapshot` switch to the bitstream; header fields routed through the same `BitWriter`/`BitReader` at full width; the leading type byte stays raw.
- `sim/snapshot.h` — unchanged signatures (maybe an include).
- `sim/CMakeLists.txt` — add `bitstream.cpp`, `quantize.cpp`; `tests/CMakeLists.txt` — add `bitstream_tests`, `quantize_tests`.
- `tests/snapshot_delta_test.cpp`, `tests/world_test.cpp` — position assertions `EXPECT_FLOAT_EQ` → `EXPECT_NEAR(expected, actual, tol)` where `tol` ~= one quantization step.

### Lifecycle / control flow

Encode (unchanged callers): `encode_keyframe`/`encode_delta` build a `BitWriter`, push the raw type byte, then write header fields and the per-entity fields (quantizing positions), then `take()`.
Decode: `apply_snapshot` reads `data[0]` for the type, constructs a `BitReader` over `data+1`, reads header fields; for a delta, checks `base.tick == baseline_tick` *before* consuming the entity body (early-out leaves `base` untouched); reads entities/removed ids; on success commits to `base`. Any `!reader.ok()` at the end (or before commit) → return false, `base` untouched.

### Edge cases

- **Quantize boundaries:** `v = kPosMax` → `2^bits - 1` (4095), `v = kPosMin` → 0. Divisor is `(2^bits - 1)`, **not** `2^bits` — using `2^bits` maps max to 4096 and overflows 12 bits (classic off-by-one).
- **Out-of-range input:** `quantize` clamps to `[kPosMin, kPosMax]` defensively (World already clamps, but the codec shouldn't trust that).
- **Round-trip is lossy:** `dequantize(quantize(v))` is within ±½ step of `v` — tests must use tolerance, not exact equality. ids and tick stay exact.
- **Truncated buffer:** a short packet makes a `read_bits` run past the end → `ok() == false` → `apply_snapshot` returns false, `base` untouched.
- **Final-byte padding:** `take()` zero-pads; the reader never interprets pad bits because `count` bounds how many entities it reads.
- **Empty world:** `count = 0` → header-only bitstream, padded; round-trips.
- **`write_bits` contract:** `bits` in `[0, 32]`; only the low `bits` bits of `value` are written; the id (≤65535) and quantized positions (≤4095) fit their widths by construction.

---

## Why this approach

- **Quantize + bit-pack positions** is the standard constant-factor lever (Quake/Source-style) and the only one that helps under 100% churn — it shrinks every field on the wire regardless of what changed. Delta (slice 1) and AOI (next slice) are orthogonal; this composes with both.
- **12-bit positions:** step 0.024 units is well under a tick's 0.33-unit motion (smooth), and takes the entity from 12 → 5 bytes (−58%). 16-bit was the alternative (half the savings, imperceptibly more precise); rejected as leaving bandwidth on the table for precision nobody sees. Per-field/adaptive precision rejected as premature.
- **Fixed 16-bit id:** dead simple, matches the existing u16 entity-count cap (≤65535). Gap-coding ids would be smaller but variable-width and fiddly — deferred.
- **One continuous bitstream after a raw type byte:** avoids interleaving byte-aligned headers with a bit-packed body (which would mis-align the delta's `removed_count`). Headers ride the stream at full width, so Fork 3 ("don't shrink headers") holds while alignment bugs vanish.
- **Tradeoffs accepted:** positions are now lossy (acceptable — sub-perceptual); the bitstream assumes same byte-order on both ends (fine for localhost/same-arch; portability deferred).

## Risks / what could go wrong

- **Lossy round-trip breaks slice-1's exact-equality tests.** `snapshot_delta_test` / `world_test` assert positions with `EXPECT_FLOAT_EQ`; quantization rounds, so they will fail unless converted to `EXPECT_NEAR(±step)`. If converted with too loose a tolerance, real bugs hide. Tolerance = one quantization step, justified in the test.
- **Bit-order mismatch (writer vs reader) decodes garbage silently** — positions land wrong with no crash. The `bitstream` round-trip test (write a known bit pattern, read it back) is the guard; it must run before the codec is wired.
- **Reading past the end not gated** → a truncated packet reads pad/garbage bits as entity data. `apply_snapshot` must check `reader.ok()` and reject; covered by a truncation test.
- **Quantize off-by-one** (`2^bits` vs `2^bits - 1` divisor) silently shifts/overflows the top of the range. Covered by a boundary test asserting `v = kPosMax` round-trips.
- **Quantize range vs World bound drift:** `quantize.h` hardcodes `[0,100]`; if `world.cpp`'s `kBound` ever grows and this isn't updated, positions clip at the old max. Documented invariant; low risk (both in-repo, both clamp).

## Success criteria

- All tests green, including:
  - **bitstream round-trip (unit):** a scripted sequence of `(value, bits)` writes reads back identical; values spanning byte boundaries; widths up to 32; empty stream; padding ignored.
  - **quantize (unit):** `dequantize(quantize(v))` within one step of `v` across `[0,100]`; `v=0` → 0 → 0 and `v=100` → 4095 → 100 (boundary exactness); out-of-range input clamps.
  - **snapshot round-trip (updated):** keyframe and delta reproduce ids and tick **exactly** and positions **within ±one step**, for empty / many / add / remove / no-change.
  - **baseline-mismatch + truncation rejection** still hold (now via `BitReader::ok`).
- **Measured headline:** at N=50 and N=100, egress drops ~58% vs the slice-1 numbers (target ~0.77 MB/s @ N=50, ~3.0 MB/s @ N=100), captured alongside the slice-1/P2 figures; tick rate holds ~60 Hz.

## Test extensions required

- New `tests/bitstream_test.cpp` and `tests/quantize_test.cpp` (the unit cases above).
- Update `tests/snapshot_delta_test.cpp` and `tests/world_test.cpp` position assertions to `EXPECT_NEAR` with a one-step tolerance.
- No new integration test: callers are unchanged and the wire change is fully covered by the codec/bitstream/quantize unit tests plus the measurement run.

---

## Decisions during implementation

<!-- Append-only; dated; non-obvious choices made while building. -->

### 2026-06-07 — `acc_` widened to 64-bit; some position asserts kept exact
- `BitWriter::acc_` is `uint64_t`, not `uint32_t` (scaffold fix): a 32-bit `write_bits` shifts the
  accumulator by 32, which is UB on a 32-bit type, and leftover bits + a 32-bit write need up to 39.
- When converting snapshot tests to `EXPECT_NEAR`, the **carry-forward** (`DeltaNoChange`) and
  **rejected-delta** (`DeltaRejectedOnBaselineMismatch`) position checks stayed `EXPECT_FLOAT_EQ`:
  those paths never quantize (entities are carried forward untouched / the delta is dropped), so the
  values are exact and asserting exactness is the stronger check.
- `quantize`/`dequantize` were delegated to Claude at Jaden's request; `BitWriter`/`BitReader` were
  hand-written by Jaden (the substantive part of the reserved piece).

## Spec amendments

<!-- Append-only; dated; times the spec was wrong mid-build. -->

---

## Future work (out-of-scope ideas surfaced during this feature)

- Gap-coded (delta-encoded) entity ids for denser id fields.
- Per-field / adaptive precision; quantize other fields (velocity) if they ever go on the wire.
- Bitstream byte-order portability if a cross-arch deploy ever matters.
- Shrink header fields (tick as delta from a session epoch, counts sized to N) if headers ever dominate.
