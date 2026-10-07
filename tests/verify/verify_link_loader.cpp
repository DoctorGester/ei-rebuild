// Checks for link_loader.cpp. Scenario: newgame (every .lnk the world view loads: 004F77C0 reads
// each part's name and archive name with 004DB9C0).
#include "verify.h"

#include "../../src/game/link_figure.h"

#include <cstring>

// 004DB9C0: PUSH ECX; MOV EAX,[EDX]; PUSH EBX; PUSH EBP (5 bytes). The original into a copy of the
// string with its own cursor, then ours; text and cursor compared.
static void (__fastcall* s_readLinkString)(stlp::string*, const char**);
static void __fastcall ReadLinkStringVerify(stlp::string* out, const char** cursor) {
    stlp::string expected(*out);
    const char* expectedCursor = *cursor;
    s_readLinkString(&expected, &expectedCursor);
    ReadLinkString(out, cursor);
    ei::verify::Check("ReadLinkString cursor", expectedCursor, *cursor, "%p");
    ei::verify::Check("ReadLinkString", true, expected == *out, "length %u", static_cast<unsigned>(out->size()));
}
EI_VERIFY(0x004DB9C0, ReadLinkStringVerify, 5, s_readLinkString);
