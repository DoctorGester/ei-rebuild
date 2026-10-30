#pragma once

// Payloads of the "crt" family (vectors.h): the game CRT's character functions. "@GameCrtCtype" is a
// fixture: isdigit, isspace and tolower for c = 0..255; the replay checks text_case.h's answers for ASCII
// against them (tests/vectors/replay_crt.cpp). Recorded by record_crt.cpp (the tag original-oracle).
//
// ASCII only: past it the game's answers are its locale's code page's (1251 / 1252) and, for a negative char, memory
// before its heap-allocated character table (they differ from run to run); ours are UTF-8's (no class, no case), and no
// record the code under test replays has a byte past ASCII.

#include "vectors.h"

namespace ei::vectors::crt {

// Record side. Offers the fixture once (the families whose code calls the character functions call it
// from their recorders, after the game has set its locale).
void OfferCtype();

} // namespace ei::vectors::crt
