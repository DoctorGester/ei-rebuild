#pragma once

// The bit reader of Bink's video and audio streams: bits taken from little-endian 32-bit words, the lowest first.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bink {

class BitReader {
public:
    // `size` bytes at `data`; past them every bit reads 0.
    BitReader(const std::uint8_t* data, std::size_t size) : data(data), size(size) {}

    // The next `count` bits (0..25) as a number, the first read lowest.
    std::uint32_t Bits(int count) {
        if (count == 0) return 0;
        const std::size_t byte = position >> 3;
        std::uint32_t value = 0;
        if (byte + sizeof(std::uint32_t) <= size) {
            std::memcpy(&value, data + byte, sizeof(value));
        } else {
            for (std::size_t i = 0; i < sizeof(value) && byte + i < size; ++i)
                value |= static_cast<std::uint32_t>(data[byte + i]) << (8 * i);
        }
        value >>= position & 7;
        position += static_cast<std::size_t>(count);
        return value & ((1u << count) - 1);
    }
    bool Bit() { return Bits(1) != 0; }
    // The next `count` bits without taking them.
    std::uint32_t Peek(int count) {
        const std::size_t at = position;
        const std::uint32_t value = Bits(count);
        position = at;
        return value;
    }
    void Skip(int count) { position += static_cast<std::size_t>(count); }
    // The bytes of the words any bit was taken from: where a stream that follows this one starts (each plane of a
    // video frame and each audio frame start on a word).
    std::size_t WordBytes() const { return (position + 31) / 32 * 4; }
    // Moves to the start of the next word unless at one.
    void AlignToWord() { position = WordBytes() * 8; }
    // Whether bits are left before the end.
    bool More() const { return position < size * 8; }

private:
    const std::uint8_t* data;
    std::size_t size;
    std::size_t position = 0;   // in bits
};

} // namespace bink
