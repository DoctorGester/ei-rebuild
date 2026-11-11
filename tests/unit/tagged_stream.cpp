// ByteStream::ReadTagged (src/engine/core/core_streams.cpp): a block is its tag, its size (counting the tag and the size) and
// its bytes; a size below 8 is a corrupt block and ends the read before anything is allocated.
#include "engine/core/core_streams.h"
#include "../../src/lib/vecmath.h"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
int checks = 0;

void Check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("ei_unit_check: tagged stream: %s\n", what);
}

// The padding check on values written whole (lib::kNoPadding): a gap after a byte, an undeclared float struct and a
// declared one.
struct ByteThenInt {
    std::uint8_t flag = 0;
    std::int32_t value = 0;
};
struct NamedGap {
    std::uint8_t flag = 0;
    std::uint8_t unused[3] = {};
    std::int32_t value = 0;
};
struct FloatPair {
    float a = 0.0f, b = 0.0f;
};
struct DeclaredPair {
    float a = 0.0f;
    std::int32_t b = 0;
};
constexpr bool NoPadding(const DeclaredPair*) { return lib::FieldsFill<&DeclaredPair::a, &DeclaredPair::b>(); }
static_assert(!lib::kFormatValue<ByteThenInt> && lib::kFormatValue<NamedGap>);
static_assert(!lib::kFormatValue<FloatPair> && lib::kFormatValue<DeclaredPair> && lib::kFormatValue<DeclaredPair[2]>);
static_assert(lib::kFormatValue<float> && lib::kFormatValue<std::array<float, 3>> && lib::kFormatValue<Vector3>);

void PutBlock(MemoryStream* stream, std::uint32_t tag, std::uint32_t size, const char* bytes, std::uint32_t count) {
    stream->Write(&tag, 4);
    stream->Write(&size, 4);
    stream->Write(bytes, count);
}

} // namespace

int RunTaggedStreamChecks() {
    {
        MemoryStream stream, block;
        PutBlock(&stream, 0x41424344, 8 + 3, "xyz", 3);
        stream.Seek(0);
        std::uint32_t tag = 0;
        Check(stream.ReadTagged(&tag, &block), "a whole block is read");
        Check(tag == 0x41424344 && block.Size() == 3, "the block's tag and bytes");
    }
    {
        MemoryStream stream, block;
        PutBlock(&stream, 1, 8, "", 0);
        stream.Seek(0);
        std::uint32_t tag = 0;
        Check(stream.ReadTagged(&tag, &block) && block.Size() == 0, "an empty block");
    }
    for (std::uint32_t size : {0u, 1u, 7u}) {
        MemoryStream stream, block;
        PutBlock(&stream, 2, size, "abcdefgh", 8);
        stream.Seek(0);
        std::uint32_t tag = 0;
        Check(!stream.ReadTagged(&tag, &block), "a size below 8 ends the read");
    }
    {
        MemoryStream stream, block;
        PutBlock(&stream, 3, 8 + 100, "ab", 2);
        stream.Seek(0);
        std::uint32_t tag = 0;
        Check(!stream.ReadTagged(&tag, &block), "a block longer than the stream ends the read");
    }
    std::printf("ei_unit_check: tagged stream: %d checks, %d failed\n", checks, failures);
    return failures;
}
