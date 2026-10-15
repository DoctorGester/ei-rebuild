// Checks ZoneExit::Contains (exit_area.cpp) against the original (pure). Reached by the client's
// exit search (CWorldClient::FindExitAt, world_client.cpp, every move order near an exit) and by
// 005677F0 (whether a group's units stand in one exit; `zone_base_bz1g`, `newgame` with the zone1
// exits). A point within a rounding step of a turned exit's edge may rarely differ (rule 7).
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/world.h"
#include "../../src/lib/mfc.h"

#include <cstddef>
#include <cstring>
#include <new>

namespace {

// 004E59F0: FLD [ECX+0x24] (3), FCOMP [0x0073B858] (6).
bool(__fastcall* s_contains)(const ZoneExit*, void*, float, float);
bool __fastcall ContainsVerify(const ZoneExit* self, void*, float x, float y) {
    bool expected = s_contains(self, nullptr, x, y);
    bool actual = self->Contains(x, y);
    ei::verify::Check("ZoneExit::Contains", expected, actual, "(%g, %g) in %g %g %g %g angle %g", x, y, self->x1,
                      self->y1, self->x2, self->y2, self->angle);
    return actual;
}

// A-S10. The readers against the original: ours reads the real stream into the real exit / string
// first, the stream is put back (position and ok flag), then the original reads into a scratch exit
// / string (freed again) and the stream is left where ours ended (SV2h1: ours allocates first); the
// fields, the strings by content and the end positions are compared. The writer: ours only (the
// bytes added). Reached by the client's zone load (00567AB0 -> 004E5750) and the server's zone
// message (world_sync.cpp -> 004E58A0) in every scenario; 004E5840 also by the save readers.

struct StreamMark {
    std::uint32_t position;
    std::uint8_t ok;
};
StreamMark Mark(CStream* s) { return {s->Position(), s->ok_}; }
void Restore(CStream* s, const StreamMark& m) {
    s->Seek(m.position);
    s->ok_ = m.ok;
}
bool SameMark(const StreamMark& a, const StreamMark& b) { return a.position == b.position && a.ok == b.ok; }

// 004E5840: PUSH -1; PUSH imm32 (7).
void(__fastcall* s_readInto)(CStream*, void*, mfc::CString*);
void __fastcall ReadIntoVerify(CStream* self, void*, mfc::CString* out) {
    StreamMark start = Mark(self);
    self->CStream::ReadInto(out);
    StreamMark mine = Mark(self);
    Restore(self, start);
    mfc::CString expected;
    s_readInto(self, nullptr, &expected);
    StreamMark end = Mark(self);
    if (!SameMark(end, mine)) Restore(self, mine);
    ei::verify::Check("CStream::ReadInto", true, std::strcmp(expected, *out) == 0 && SameMark(end, mine),
                      "\"%s\" at %u", static_cast<const char*>(*out), start.position);
}

// 004E5750: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC] (6).
void(__fastcall* s_exitRead)(ZoneExit*, void*, CStream*);
void __fastcall ExitReadVerify(ZoneExit* self, void*, CStream* stream) {
    StreamMark start = Mark(stream);
    alignas(ZoneExit) unsigned char scratchBytes[sizeof(ZoneExit)];
    std::memcpy(scratchBytes, self, sizeof(scratchBytes));
    self->ZoneExit::Read(stream);
    StreamMark mine = Mark(stream);
    Restore(stream, start);
    auto* scratch = reinterpret_cast<ZoneExit*>(scratchBytes);
    auto* scratchTarget = new (&scratch->target) mfc::CString();
    s_exitRead(scratch, nullptr, stream);
    StreamMark end = Mark(stream);
    if (!SameMark(end, mine)) Restore(stream, mine);
    bool ok = std::memcmp(scratch, self, offsetof(ZoneExit, target)) == 0 &&
              std::strcmp(scratch->target, self->target) == 0 &&
              std::memcmp(&scratch->entrance, &self->entrance, offsetof(ZoneExit, hasDeployAngle) - offsetof(ZoneExit, entrance)) == 0 &&
              scratch->hasDeployAngle == self->hasDeployAngle && scratch->deployAngle == self->deployAngle &&
              SameMark(end, mine);
    ei::verify::Check("ZoneExit::Read", true, ok, "\"%s\" version %u", self->target, stream->version_);
    scratchTarget->~CString();
}

// 004E58A0: ours only.
void* s_unusedExitWrite;
void __fastcall ExitWriteVerify(const ZoneExit* self, void*, CStream* stream) {
    std::uint32_t before = stream->Position();
    self->ZoneExit::Write(stream);
    std::uint32_t expected = 13 * 4 + 1 + static_cast<std::uint32_t>(std::strlen(self->target) + 1);
    ei::verify::Check("ZoneExit::Write", true, stream->Position() - before == expected, "%u bytes, expected %u",
                      stream->Position() - before, expected);
}

} // namespace

EI_VERIFY(0x004E59F0, ContainsVerify, 9, s_contains);
EI_VERIFY(0x004E5840, ReadIntoVerify, 7, s_readInto);
EI_VERIFY(0x004E5750, ExitReadVerify, 6, s_exitRead);
EI_VERIFY(0x004E58A0, ExitWriteVerify, 0, s_unusedExitWrite);
