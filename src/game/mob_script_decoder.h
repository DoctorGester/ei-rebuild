#pragma once

// The decoder of the .mob file's coded script text (chunk 0xACCEECCB) and of encoded chunk strings
// two STLport vector<char>, the decoded text and the coded input. The input's first dword
// seeds the MSVC rand LCG (x = x * 0x343FD + 0x269EC3); each following byte is XORed with x >> 16.
// Users: CWorldServer::LoadZoneScripts (world_game.cpp), Chunk::ReadEncodedString
// (chunk_stream.cpp, its own vector declaration and members).

#include <cstddef>
#include <string>

#include <vector>

struct MobScriptDecoder {
    std::vector<char> text;    // decoded
    std::vector<char> input;   // coded: the seed, then the bytes

    // `text` = the input after the seed, unscrambled; `out` = the text up to its
    // first zero byte (CString ReleaseBuffer(-1)).
    void Decode(std::string* out);
};
