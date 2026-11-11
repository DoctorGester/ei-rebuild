// The byte buffers, their cursor and the LZ coder (src/engine/core/coder.h): sizes and growth, reads past the end,
// the strings, the cursor's byte and bit layout, reads bounded by the cursor's limit and a coder round trip.
#include "engine/core/coder.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void InitCodeTables();   // core/coder.cpp

namespace {

int failures = 0;
int checks = 0;

void Check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("ei_unit_check: game buffer: %s\n", what);
}

std::int32_t Capacity(const GameBuffer& b) { return static_cast<std::int32_t>(b.end - b.begin); }

void BufferChecks() {
    GrowingBuffer b;
    Check(Capacity(b) == 0x20 && b.Size() == 0, "a new buffer: 0x20 bytes, none written");
    b.Put(std::uint8_t{0x12});
    b.Put(std::uint32_t{0xA1B2C3D4});
    Check(b.Size() == 5 && b.begin[0] == 0x12 && b.begin[1] == 0xD4 && b.begin[4] == 0xA1, "puts in place");
    b.Seek(0);
    Check(b.Unread() == 5, "five bytes unread after a rewind");
    std::uint8_t first = 0;
    std::uint32_t second = 0;
    b.Get(first);
    b.Get(second);
    Check(first == 0x12 && second == 0xA1B2C3D4 && !b.readPastEnd, "gets read the puts back");
    std::uint32_t past = 0xFFFFFFFF;
    b.Get(past);
    Check(past == 0 && b.readPastEnd && b.cur == b.high, "a read past the end: zeros, the error, the cursor at high");
    b.Seek(3);
    std::uint32_t half = 0xFFFFFFFF;
    b.Get(half);
    Check(half == 0x0000A1B2 && b.cur == b.high, "a read across the end: what is left, then zeros");
    b.Clear();
    Check(b.Size() == 0 && b.cur == b.begin && !b.readPastEnd, "Clear empties the buffer and the error");

    // Growth: (cursor + size) * 2 + 0x40, the bytes kept.
    GrowingBuffer g;
    g.Put(std::uint8_t{7});
    std::vector<std::uint8_t> bytes(100, 0x5A);
    g.Write(bytes.data(), 100);
    Check(Capacity(g) == 101 * 2 + 0x40 && g.Size() == 101 && g.begin[0] == 7 && g.begin[100] == 0x5A,
          "a write past the end grows the block and keeps the bytes");
    GrowingBuffer s;
    s.Seek(50);
    Check(Capacity(s) == 50 * 2 + 0x40 && s.high == s.begin && s.Size() == 50,
          "a seek past the end grows to twice the position; Size then counts up to the cursor");
    s.SetSize(10);
    Check(s.cur == s.begin && s.Size() == 10 && Capacity(s) == 50 * 2 + 0x40, "SetSize within the block");
    s.SetSize(300);
    Check(s.Size() == 300 && Capacity(s) == 300 * 2 + 0x40, "SetSize past the end grows the block");

    // A copy keeps the bytes, the offsets and the error.
    b.Put(std::uint16_t{0xBEEF});
    b.readPastEnd = true;
    GrowingBuffer copy(b);
    Check(copy.Size() == 2 && copy.begin[0] == 0xEF && copy.readPastEnd && copy.begin != b.begin, "a copy");

    // A view: writes past its end are dropped.
    std::uint8_t memory[4] = {1, 2, 3, 4};
    BufferView view(memory, memory + 4);
    Check(view.Size() == 4, "a view is all written");
    view.Seek(2);
    view.Put(std::uint32_t{0xFFFFFFFF});
    Check(memory[2] == 3 && view.cur == memory + 2, "a view drops a write past its end");
}

void StringChecks() {
    GrowingBuffer b;
    const std::string shortText = "lobby";
    const std::string longText(200, 'x');
    b.WriteString(shortText);
    b.WriteString(longText);
    Check(b.begin[0] == 10 && b.Size() == 1 + 5 + 4 + 200, "string headers: length * 2, or * 2 + 1 in four bytes");
    b.Seek(0);
    std::string a, c;
    Check(b.ReadString(&a, 0x100) && b.ReadString(&c, 0x100) && a == shortText && c == longText,
          "strings read back");
    b.Seek(0);
    Check(!b.ReadString(&a, 4) && b.readPastEnd, "a string over the limit is refused with the error");

    // A length past the high mark stops there, with the error, even with room left in the block.
    GrowingBuffer cut;
    const std::string cutText = "abcdef";
    cut.WriteString(cutText);
    cut.Seek(0);
    cut.high -= 3;
    std::string got;
    Check(cut.ReadString(&got, 0) && got == "abc" && cut.readPastEnd && cut.cur == cut.high,
          "a string past the high mark is cut there, with the error");
    GrowingBuffer huge;
    huge.Put(std::uint32_t{0xFFFFFFF1});
    huge.Seek(0);
    Check(huge.ReadString(&got, 0) && got.empty() && huge.readPastEnd && huge.cur == huge.high,
          "a huge length reads nothing past the end");
}

void CursorChecks() {
    GrowingBuffer b;
    BufferCursor w;
    w.BeginWrite(&b, 0x40);
    w.PutBits(1, 1);
    w.Put(std::uint8_t{0xAB});
    w.PutBits(2, 2);
    w.Put(std::uint16_t{0x1234});
    w.PutString("hi");
    w.End();
    const std::uint8_t expected[] = {0x05, 0xAB, 0x34, 0x12, 'h', 'i', 0};
    Check(b.Size() == 7 && std::memcmp(b.begin, expected, sizeof(expected)) == 0 && !w.sink,
          "bits share the first reserved byte; bytes follow in order; End commits");

    b.Seek(0);
    BufferCursor r;
    r.BeginRead(&b, b.Size());
    std::uint8_t byte = 0;
    std::uint16_t word = 0;
    std::string text;
    const std::uint32_t bit = r.GetBit();
    r.Take(byte);
    const std::uint32_t two = r.GetBits(2);
    r.Take(word);
    r.TakeString(text);
    Check(bit == 1 && byte == 0xAB && two == 2 && word == 0x1234 && text == "hi" && r.p == r.limit, "read back");

    // Reads stop at the limit.
    std::uint8_t three[3] = {0x11, 0x22, 0x33};
    BufferView view(three, three + 3);
    BufferCursor bounded;
    bounded.BeginRead(&view, 3);
    std::uint32_t dword = 0xFFFFFFFF;
    bounded.Take(dword);
    Check(dword == 0x00332211 && bounded.p == three + 3, "a read across the limit: zeros past it, p at the limit");
    Check(bounded.GetBits(12) == 0 && bounded.GetBit() == 0 && bounded.p == three + 3, "bits past the limit are 0");
    std::uint8_t unterminated[4] = {'a', 'b', 'c', 'd'};
    BufferView textView(unterminated, unterminated + 4);
    BufferCursor t;
    t.BeginRead(&textView, 3);
    t.TakeString(text);
    Check(text == "abc" && t.p == unterminated + 3, "a text without its zero byte ends at the limit");
}

void CoderChecks() {
    InitCodeTables();
    std::vector<std::uint8_t> input;
    for (int i = 0; i < 3000; ++i) input.push_back(static_cast<std::uint8_t>((i * 7) % 13 + (i / 500)));
    BufferView view(input.data(), input.data() + input.size());
    GrowingBuffer packed;
    CoderState coder;
    coder.Compress(&view, &packed);
    Check(packed.Size() > 4 && packed.Size() < static_cast<std::int32_t>(input.size()), "repeats compress");
    std::int32_t stored = 0;
    std::memcpy(&stored, packed.begin, 4);
    Check(stored == static_cast<std::int32_t>(input.size()), "the stream starts with the plain size");

    packed.Seek(0);
    GrowingBuffer plain;
    CoderState decoder;
    decoder.Decompress(&packed, &plain);
    Check(plain.Size() == static_cast<std::int32_t>(input.size()) &&
              std::memcmp(plain.begin, input.data(), input.size()) == 0,
          "a compressed stream decompresses to its input");

    // Cut short, the decoder stops at the end of the bytes and still writes the stored size.
    GrowingBuffer cut;
    cut.Write(packed.begin, 40);
    cut.Seek(0);
    GrowingBuffer partial;
    CoderState shortDecoder;
    shortDecoder.Decompress(&cut, &partial);
    Check(partial.Size() == static_cast<std::int32_t>(input.size()) && cut.cur == cut.begin + 40,
          "a cut stream reads no further than its end");
}

} // namespace

int RunGameBufferChecks() {
    BufferChecks();
    StringChecks();
    CursorChecks();
    CoderChecks();
    std::printf("ei_unit_check: game buffer: %d checks, %d failed\n", checks, failures);
    return failures;
}
