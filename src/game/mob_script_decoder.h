#pragma once

// The decoder of the .mob file's coded script text (chunk 0xACCEECCB) and of encoded chunk strings
// (A-S7): two STLport vector<char>, the decoded text and the coded input. The input's first dword
// seeds the MSVC rand LCG (x = x * 0x343FD + 0x269EC3); each following byte is XORed with x >> 16.
// Users: CWorldServer::LoadZoneScripts 00596350 (world_game.cpp), Chunk::ReadEncodedString
// 0042C650 (chunk_stream.cpp, its own vector declaration and members).

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>

struct MobScriptDecoder {
    stlp::vector<char> text;    // +0x00 decoded
    stlp::vector<char> input;   // +0x0C coded: the seed, then the bytes

    // 004D8E10 (RET 4). `text` = the input after the seed, unscrambled; `out` = the text up to its
    // first zero byte (CString ReleaseBuffer(-1)).
    void Decode(mfc::CString* out);
};
static_assert(offsetof(MobScriptDecoder, input) == 0x0C);
static_assert(sizeof(MobScriptDecoder) == 0x18);
