#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sim {

// Writes values at arbitrary bit widths, packed back-to-back (MSB-first within each
// byte). The final partial byte is zero-padded by take().
class BitWriter {
public:
    // Append the low `bits` bits of value (bits in [0, 32]).
    void write_bits(uint32_t value, int bits);

    // Flush: zero-pad the partial final byte and return the packed buffer.
    std::vector<uint8_t> take();

private:
    std::vector<uint8_t> out_;
    uint64_t acc_ = 0;  // pending bits not yet flushed to a full byte (64-bit: room for
                        // up to 7 leftover bits + a full 32-bit write without overflow)
    int nbits_ = 0;     // count of pending bits in acc_
};

// Reads values written by BitWriter, MSB-first. A read that runs off the end leaves
// the returned value unspecified and latches ok() to false; callers check ok()
// before trusting decoded data.
class BitReader {
public:
    BitReader(const uint8_t* data, size_t len);

    // Read the next `bits` bits (bits in [0, 32]) as an unsigned value.
    uint32_t read_bits(int bits);

    // False once any read has run past the end of the buffer.
    bool ok() const;

private:
    const uint8_t* data_;
    size_t len_;         // length in bytes
    size_t bitpos_ = 0;  // next bit to read, counted from the MSB of byte 0
    bool ok_ = true;
};

}  // namespace sim
