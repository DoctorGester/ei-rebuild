// Check for model_names.cpp: ours into the real CString, then the original into a scratch CString
// with its own cursor (SV2h1: ours allocates first); text and cursor compared. Scenario: every model load with skeleton links (newgame, models).
#include "verify.h"

#include "../../src/game/model.h"

#include <cstring>

// 004DBCA0: SUB ESP,8; PUSH EBX; MOV EBX,EDX (6 bytes).
static void (__fastcall* s_readName)(mfc::CString*, const std::uint8_t**);
static void __fastcall ReadNameVerify(mfc::CString* out, const std::uint8_t** cursor) {
    const std::uint8_t* expectedCursor = *cursor;
    ReadPrefixedName(out, cursor);
    mfc::CString expected;
    s_readName(&expected, &expectedCursor);
    ei::verify::Check("ReadPrefixedName cursor", expectedCursor, *cursor, "%p");
    ei::verify::Check("ReadPrefixedName", 0, std::strcmp(expected, *out), "%d");
}
EI_VERIFY(0x004DBCA0, ReadNameVerify, 6, s_readName);
