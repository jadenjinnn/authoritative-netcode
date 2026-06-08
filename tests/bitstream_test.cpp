#include <gtest/gtest.h>

#include "bitstream.h"

using sim::BitReader;
using sim::BitWriter;

TEST(BitStream, RoundTripSingleValue) {
    BitWriter w;
    w.write_bits(0xABC, 12);
    std::vector<uint8_t> buf = w.take();

    BitReader r(buf.data(), buf.size());
    EXPECT_EQ(r.read_bits(12), 0xABCu);
    EXPECT_TRUE(r.ok());
}

TEST(BitStream, RoundTripSequenceVariousWidths) {
    BitWriter w;
    w.write_bits(1, 1);
    w.write_bits(5, 3);
    w.write_bits(0x2A, 6);
    w.write_bits(0xBEEF, 16);
    w.write_bits(0, 4);
    std::vector<uint8_t> buf = w.take();

    BitReader r(buf.data(), buf.size());
    EXPECT_EQ(r.read_bits(1), 1u);
    EXPECT_EQ(r.read_bits(3), 5u);
    EXPECT_EQ(r.read_bits(6), 0x2Au);
    EXPECT_EQ(r.read_bits(16), 0xBEEFu);
    EXPECT_EQ(r.read_bits(4), 0u);
    EXPECT_TRUE(r.ok());
}

TEST(BitStream, FullWidth32) {
    BitWriter w;
    w.write_bits(0xDEADBEEF, 32);
    std::vector<uint8_t> buf = w.take();

    BitReader r(buf.data(), buf.size());
    EXPECT_EQ(r.read_bits(32), 0xDEADBEEFu);
    EXPECT_TRUE(r.ok());
}

TEST(BitStream, PadsFinalByte) {
    BitWriter w;
    w.write_bits(0xABC, 12);  // 12 bits -> 2 bytes, 4 pad bits
    std::vector<uint8_t> buf = w.take();
    EXPECT_EQ(buf.size(), 2u);

    BitReader r(buf.data(), buf.size());
    EXPECT_EQ(r.read_bits(12), 0xABCu);  // pad bits are never interpreted
    EXPECT_TRUE(r.ok());
}

TEST(BitStream, EmptyWriterYieldsEmptyBuffer) {
    BitWriter w;
    EXPECT_TRUE(w.take().empty());
}

TEST(BitStream, ReadPastEndSetsNotOk) {
    BitWriter w;
    w.write_bits(0xF, 4);
    std::vector<uint8_t> buf = w.take();  // 1 byte

    BitReader r(buf.data(), buf.size());
    r.read_bits(4);
    EXPECT_TRUE(r.ok());
    r.read_bits(8);  // only 4 pad bits remain in the byte; 8 > available -> past end
    EXPECT_FALSE(r.ok());
}
