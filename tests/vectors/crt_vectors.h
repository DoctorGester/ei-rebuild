#pragma once

// Payloads of the "crt" family (vectors.h): the game CRT's character functions. "@GameCrtCtype" is a
// fixture: isdigit, isspace and tolower for c = 0..255, which ei_vectors' gamecrt functions answer
// from (tests/vectors/replay_crt.cpp). Recorded by record_crt.cpp.
//
// The game sets a locale (LC_CTYPE); its character table is then on the heap, and isdigit / isspace
// of a negative char (a signed char >= 0x80, as the script lexer passes them) read memory before it:
// the answers differ from run to run. A record whose input has such bytes carries what the two
// answered at the call (NegativeClasses).

#include "vectors.h"

#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>

namespace ei::vectors::crt {

// isdigit and isspace (nonzero or not) of c = -128..-1, one bit per char (bit (c + 128) & 7 of byte
// (c + 128) >> 3).
struct NegativeClasses {
    std::uint8_t digit[16];
    std::uint8_t space[16];
};

// Record side. Offers the fixture once (the families whose code calls the character functions call it
// from their recorders, after the game has set its locale).
void OfferCtype();
// A u8 flag, then NegativeClasses when `text` (`size` bytes) has a byte >= 0x80.
void PutNegativeClasses(Writer& w, const void* text, std::size_t size);

// Replay side. Reads what PutNegativeClasses wrote; gamecrt answers negative chars from it until the
// scope ends (without it they abort the replay).
class NegativeClassesScope {
public:
    explicit NegativeClassesScope(Reader& r);
    ~NegativeClassesScope();
    NegativeClassesScope(const NegativeClassesScope&) = delete;
    NegativeClassesScope& operator=(const NegativeClassesScope&) = delete;
};

} // namespace ei::vectors::crt
