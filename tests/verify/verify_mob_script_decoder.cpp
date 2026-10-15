// Check for mob_script_decoder.cpp. Scenarios: zone_gz2g_traps (zone2.mob's scripts are coded,
// CWorldServer::LoadZoneScripts), world_load; any encoded chunk string (Chunk::ReadEncodedString).
#include "verify.h"

#include "../../src/game/mob_script_decoder.h"

#include <cstring>

// 004D8E10: PUSH ECX; PUSH ESI; MOV ESI,ECX; LEA EAX,[ESP+7] (8 bytes). Ours runs first, then the
// original on a scratch decoder (an empty text vector, the same input bytes, which it only reads)
// into a scratch CString, both freed again (SV2h1: ours allocates first); the decoded bytes and the
// strings compared.
static void (__fastcall* s_decode)(MobScriptDecoder*, void*, mfc::CString*);
static void __fastcall DecodeVerify(MobScriptDecoder* decoder, void*, mfc::CString* out) {
    decoder->Decode(out);
    MobScriptDecoder scratch;
    scratch.input.Start() = decoder->input.begin();
    scratch.input.Finish() = decoder->input.end();
    scratch.input.EndOfStorage() = decoder->input.end();
    mfc::CString expected;
    s_decode(&scratch, nullptr, &expected);
    const std::size_t size = scratch.text.size();
    ei::verify::Check("MobScriptDecoder text", true,
                      size == decoder->text.size() && std::memcmp(scratch.text.begin(), decoder->text.begin(), size) == 0,
                      "%u bytes", static_cast<unsigned>(size));
    ei::verify::Check("MobScriptDecoder string", 0, std::strcmp(expected, *out), "%d");
    expected.Empty();   // made from the text: freed before it
    scratch.text.FreeStorage();
}
EI_VERIFY(0x004D8E10, DecodeVerify, 8, s_decode);
