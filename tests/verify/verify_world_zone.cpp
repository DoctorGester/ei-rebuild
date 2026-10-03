// Checks for the world server's zone loading, map objects and AddUnitToServer
// (src/game/world_zone.cpp). Every function here allocates, opens files, creates or removes objects
// or writes a save stream, so only ours runs (stolenBytes 0) and the resulting state is checked.
#include "verify.h"

#include "../../src/game/object.h"
#include "../../src/game/world.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

bool SameText(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}

// Whether `to` is a copy of `from`: the bytes before the target, the target text, the fields after.
bool SameExit(const ZoneExit& from, const ZoneExit& to) {
    auto* a = reinterpret_cast<const std::uint8_t*>(&from);
    auto* b = reinterpret_cast<const std::uint8_t*>(&to);
    return std::memcmp(a, b, 0x28) == 0 && SameText(from.target, to.target) && std::memcmp(a + 0x2C, b + 0x2C, 9) == 0 &&
           std::memcmp(a + 0x38, b + 0x38, 4) == 0;
}
} // namespace

// 005915E0: the exits are exactly the source records whose target is not "", in order.
static void __fastcall SetZoneExitsVerify(CWorldServer* self, void*, const ZoneExitArray* source) {
    self->SetZoneExits(source);
    bool ok = true;
    int copied = 0;
    for (int i = 0; i < source->size && ok; ++i) {
        const ZoneExit& from = source->data[i];
        if (SameText(from.target, "")) continue;
        ok = copied < self->exitCount_ && SameExit(from, self->exits_[copied]);
        ++copied;
    }
    ok = ok && copied == self->exitCount_ && (self->exitCount_ == 0 || self->exits_ != nullptr);
    ei::verify::Check("CWorldServer::SetZoneExits", true, ok, "source %d copied %d exits %d", source->size, copied,
                      self->exitCount_);
}
EI_VERIFY(0x005915E0, SetZoneExitsVerify, 0, s_unused);

// 005910A0: afterwards the world has an object with the requested id (the name's text key).
static void __fastcall AddUnitVerify(CWorldServer* self, void*, unsigned id, const char* prototype, const char* name,
                                     const Vector3* position) {
    self->AddUnitToServer(id, prototype, name, position);
    CEngineObject* unit = self->FindObject(id);
    bool ok = unit && unit->GetClassId() == world::kClassUnit;
    ei::verify::Check("CWorldServer::AddUnitToServer", true, ok, "%s (%s) id %u", name ? name : "", prototype ? prototype : "",
                      id);
}
EI_VERIFY(0x005910A0, AddUnitVerify, 0, s_unused);

// 00596030: reads a file (and creates objects): returns 1 when the file was read.
static int __fastcall LoadMapObjectsVerify(CWorldServer* self, void*, const char* file, int keepIds) {
    std::size_t before = self->objects_.map.Size();
    int result = self->LoadMapObjects(file, keepIds);
    ei::verify::Check("CWorldServer::LoadMapObjects", 1, result, "%s keep %d: objects %u -> %u", file ? file : "", keepIds,
                      static_cast<unsigned>(before), static_cast<unsigned>(self->objects_.map.Size()));
    return result;
}
EI_VERIFY(0x00596030, LoadMapObjectsVerify, 0, s_unused);

// 00595AB0: returns 1 and the file is then on the loaded list.
static int __fastcall LoadMobFileVerify(CWorldServer* self, void*, const char* file) {
    int result = self->LoadMobFile(file);
    bool listed = false;
    for (int i = 0; i < self->mobFiles_.size && !listed; ++i)
        listed = file ? SameText(self->mobFiles_.data[i], file) : self->mobFiles_.data[i][0] == '\0';
    ei::verify::Check("CWorldServer::LoadMobFile", true, result == 1 && listed, "%s -> %d listed %d", file ? file : "(null)",
                      result, listed);
    return result;
}
EI_VERIFY(0x00595AB0, LoadMobFileVerify, 0, s_unused);

// 00596C60: no map object left (unless the walk stopped at an empty entry), the missile and
// counted lists empty, the countdowns cleared.
static void __fastcall ClearMapObjectsVerify(CWorldServer* self, void*) {
    self->ClearMapObjects();
    auto* first = self->mapObjects_.map.First();
    bool objectsGone = !first || !first->value;
    const auto& missiles = self->projectiles_.list;
    const auto& counted = self->counted_;
    bool listsEmpty = !missiles.head && !missiles.tail && missiles.count == 0 && !missiles.blocks && !counted.head &&
                      !counted.tail && counted.count == 0 && !counted.blocks;
    bool countdownsClear = At<void*>(self, 0x74) == nullptr && At<int>(self, 0x78) == 0 && At<int>(self, 0x7C) == 0;
    ei::verify::Check("CWorldServer::ClearMapObjects", true, objectsGone && listsEmpty && countdownsClear,
                      "objects left %u, lists %d, countdowns %d", static_cast<unsigned>(self->mapObjects_.map.Size()),
                      listsEmpty, countdownsClear);
}
EI_VERIFY(0x00596C60, ClearMapObjectsVerify, 0, s_unused);

// 00596730: on a memory stream the write position moved by at least the terminator, which is the
// last thing written (-1).
static void __fastcall SaveMapObjectsVerify(CWorldServer* self, void*, void* stream) {
    bool memory = *static_cast<void**>(stream) == reinterpret_cast<void*>(0x0073BD90);
    auto* ms = static_cast<CMemoryStream*>(stream);
    std::uint32_t before = memory ? ms->position_ : 0;
    self->SaveMapObjects(stream);
    if (!memory) return;
    bool ok = ms->position_ >= before + 4 && ms->data_;
    if (ok) {
        std::uint32_t last;
        std::memcpy(&last, ms->data_ + ms->position_ - 4, 4);
        ok = last == 0xFFFFFFFFu;
    }
    ei::verify::Check("CWorldServer::SaveMapObjects", true, ok, "position %u -> %u, objects %u", before, ms->position_,
                      static_cast<unsigned>(self->mapObjects_.map.Size()));
}
EI_VERIFY(0x00596730, SaveMapObjectsVerify, 0, s_unused);

// 00595080: PUSH -1; PUSH 0x007263F8 (7 bytes). Writes only the loading movie name (00791EE0): the
// original runs first, ours from the same starting value, and the chosen names are compared as text
// (addon.dll's FixProgressNames points at its own copies). With the flag on in a Rebirth launch this
// compares with the patched original.
static void(__stdcall* s_selectLoadingMovie)(const char*);
static void __stdcall SelectLoadingMovieVerify(const char* zone) {
    const char*& movie = *reinterpret_cast<const char**>(0x00791EE0);
    const char* before = movie;
    s_selectLoadingMovie(zone);
    const char* expected = movie;
    movie = before;
    SelectLoadingMovie(zone);
    bool same = expected && movie && std::strcmp(expected, movie) == 0;
    ei::verify::Check("SelectLoadingMovie", true, same, "zone %s: %s vs %s", zone ? zone : "(null)",
                      expected ? expected : "(null)", movie ? movie : "(null)");
}
EI_VERIFY(0x00595080, SelectLoadingMovieVerify, 7, s_selectLoadingMovie);
