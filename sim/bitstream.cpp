#include "bitstream.h"

namespace sim
{

    void BitWriter::write_bits(uint32_t value, int bits)
    {
        acc_ <<= bits;
        acc_ |= ((uint64_t{1}<<bits)-1)&value;
        nbits_ += bits;

        while (nbits_ >= 8){
            uint8_t byte = (acc_ >> (nbits_ - 8)) & 0xFF;
            out_.push_back(byte);
            nbits_ -= 8;
        }
    }

    std::vector<uint8_t> BitWriter::take()
    {
        if (nbits_ > 0) {
            uint8_t byte = (acc_ << (8-nbits_)) & 0xFF;
            out_.push_back(byte);
            nbits_ = 0;
        }

        return out_;
    }

    BitReader::BitReader(const uint8_t *data, size_t len)
        : data_(data), len_(len)
    {
    }

    uint32_t BitReader::read_bits(int bits)
    {
        if ((bitpos_ + bits) > len_ * 8) {
            ok_ = false;
            return 0;
        }

        uint32_t result = 0;

        for (int i = 0; i<bits; i++){
            result = (result << 1) | (data_[bitpos_/8] >> (7 - bitpos_%8)) & 1;
            ++bitpos_;
        }

        return result; 
    }

    bool BitReader::ok() const
    {
        return ok_;
    }

} // namespace sim
