// Checks for the world's save and load (src/game/world_save.cpp).
//
// SaveWorld only writes a stream (its side effects, the players' "gtime" update and releasing the
// managers' unreferenced objects, do nothing the second time), so the original writes into a
// scratch memory stream first and ours into the real one; the bytes are compared. The loaders
// create objects and read the stream, so only ours runs and the resulting state is checked.
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/object.h"
#include "../../src/game/world.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

using SaveWorldFn = void(__fastcall*)(CWorldServer*, void*, WorldStream*);
SaveWorldFn s_originalSaveWorld;

bool IsMemoryStream(const void* stream) {
    return *static_cast<void* const*>(stream) == reinterpret_cast<void*>(kMemoryStreamVtable);
}

// The objects the map-object walk reaches (it stops at the first empty entry).
std::size_t ReachableObjects(const ObjectIdMap& map) {
    std::size_t n = 0;
    for (auto* node = map.map.First(); node && node->value; node = map.map.After(node)) ++n;
    return n;
}

// Whether every map object (as far as the walk reaches) is registered by its id in the world.
bool MapObjectsRegistered(CWorldServer* world) {
    for (auto* node = world->mapObjects_.map.First(); node && node->value; node = world->mapObjects_.map.After(node)) {
        auto* found = world->objects_.map.Find(static_cast<unsigned>(node->value->id_));
        if (!found || found->value != node->value || static_cast<unsigned>(node->value->id_) != node->key) return false;
    }
    return true;
}
} // namespace

// 00597BD0: the original into a scratch memory stream, ours into the real one; bytes compared.
// Each player's party records end with the gold XORed with a fresh random word (00661C10), so
// the random generator (IsaacRandom at 007A9D40) is put back before ours runs: both draw the
// same words, and the game's random sequence is the one it would be without the check.
static void __fastcall SaveWorldVerify(CWorldServer* self, void*, WorldStream* stream) {
    if (!IsMemoryStream(stream)) {
        self->SaveWorld(stream);
        return;
    }
    auto* random = reinterpret_cast<std::uint8_t*>(0x007A9D40);
    constexpr std::size_t kRandomSize = 0x814;
    static std::uint8_t randomBefore[kRandomSize];
    static std::uint8_t randomAfterOriginal[kRandomSize];
    std::memcpy(randomBefore, random, kRandomSize);

    auto* real = reinterpret_cast<CMemoryStream*>(stream);
    alignas(4) std::uint8_t storage[sizeof(CMemoryStream)];
    auto* scratch = MemoryStreamConstruct(reinterpret_cast<CMemoryStream*>(storage));
    scratch->unknown_8 = real->unknown_8;   // the save version
    s_originalSaveWorld(self, nullptr, reinterpret_cast<WorldStream*>(scratch));
    std::memcpy(randomAfterOriginal, random, kRandomSize);
    std::memcpy(random, randomBefore, kRandomSize);

    std::uint32_t before = real->position_;
    self->SaveWorld(stream);
    ei::verify::Check("CWorldServer::SaveWorld(random)", true, std::memcmp(random, randomAfterOriginal, kRandomSize) == 0,
                      "the same random words drawn");
    std::uint32_t written = real->position_ - before;
    bool sameSize = written == scratch->position_;
    std::uint32_t firstDifference = 0xFFFFFFFFu;
    if (sameSize && written) {
        for (std::uint32_t i = 0; i < written; ++i) {
            if (real->data_[before + i] != scratch->data_[i]) {
                firstDifference = i;
                break;
            }
        }
    }
    ei::verify::Check("CWorldServer::SaveWorld", true, sameSize && firstDifference == 0xFFFFFFFFu,
                      "original %u bytes, ours %u, first difference at %d", scratch->position_, written,
                      static_cast<int>(firstDifference));
    MemoryStreamDestruct(scratch);
}
EI_VERIFY(0x00597BD0, SaveWorldVerify, 7, s_originalSaveWorld);

// 00596E70: returns 1 for a save that loads, without reading past the end of the stream; the
// world is marked loaded and the map objects are all registered.
static int __fastcall LoadSaveVerify(CWorldServer* self, void*, WorldStream* stream, const char* map) {
    int result = self->LoadSave(stream, map);
    bool memory = IsMemoryStream(stream);
    auto* ms = reinterpret_cast<CMemoryStream*>(stream);
    bool ok = result == 1 && self->loaded_ == 1 && (!memory || ms->ok_) && MapObjectsRegistered(self);
    ei::verify::Check("CWorldServer::LoadSave", true, ok, "result %d loaded %d stream ok %d at %u of %u, version %d", result,
                      self->loaded_, memory ? ms->ok_ : -1, memory ? ms->position_ : 0u, memory ? ms->size_ : 0u,
                      stream->version);
    return result;
}
EI_VERIFY(0x00596E70, LoadSaveVerify, 0, s_unused);

// 00596900: afterwards the map objects are registered by id in the world's object map, and no
// read ran past the end of a memory stream.
static void __fastcall LoadSavedMapObjectsVerify(CWorldServer* self, void*, WorldStream* stream) {
    std::size_t before = ReachableObjects(self->mapObjects_);
    self->LoadSavedMapObjects(stream);
    bool memory = IsMemoryStream(stream);
    bool streamOk = !memory || reinterpret_cast<CMemoryStream*>(stream)->ok_;
    std::size_t after = ReachableObjects(self->mapObjects_);
    ei::verify::Check("CWorldServer::LoadSavedMapObjects", true, streamOk && MapObjectsRegistered(self),
                      "map objects %u -> %u, stream ok %d", static_cast<unsigned>(before), static_cast<unsigned>(after),
                      streamOk);
}
EI_VERIFY(0x00596900, LoadSavedMapObjectsVerify, 0, s_unused);

// 0058D280: every player's last tick is the world's tick afterwards.
static void __fastcall UpdatePlayerTimesVerify(CWorldServer* self, void*, float offset) {
    self->UpdatePlayerTimes(offset);
    bool ok = true;
    int players = 0;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(self->players_); player; player = it.Next(), ++players)
        ok = ok && player->lastTick == self->tick_;
    ei::verify::Check("CWorldServer::UpdatePlayerTimes", true, ok, "%d players, tick %d", players, self->tick_);
}
EI_VERIFY(0x0058D280, UpdatePlayerTimesVerify, 0, s_unused);

namespace {
// A shared-object entry (0x20 bytes): id, player, value, CArray<CRefPtr> of objects.
struct SharedEntry {
    int id;
    int player;
    std::uint32_t value;
    void* arrayVtable;
    CEngineObject** data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(SharedEntry) == 0x20);

using LoadSharedFn = void(__fastcall*)(void*, void*, WorldStream*);
LoadSharedFn s_originalLoadShared;
} // namespace

// 00589CA0: on a memory stream and a fresh entry, the original first reads into a scratch entry
// (its references are released afterwards) and the stream is rewound; the fields, the objects and
// the bytes read are compared.
static void __fastcall LoadSharedObjectVerify(void* entryArg, void*, WorldStream* stream) {
    auto* entry = static_cast<SharedEntry*>(entryArg);
    if (!IsMemoryStream(stream) || entry->data || entry->size) {
        LoadSharedObject(entry, nullptr, stream);
        return;
    }
    auto* ms = reinterpret_cast<CMemoryStream*>(stream);
    std::uint32_t start = ms->position_;
    std::uint8_t okBefore = ms->ok_;
    SharedEntry scratch = *entry;
    s_originalLoadShared(&scratch, nullptr, stream);
    std::uint32_t originalEnd = ms->position_;
    ms->position_ = start;
    ms->ok_ = okBefore;
    LoadSharedObject(entry, nullptr, stream);

    bool ok = ms->position_ == originalEnd && scratch.id == entry->id && scratch.player == entry->player &&
              scratch.value == entry->value && scratch.size == entry->size;
    for (int i = 0; ok && i < entry->size; ++i) ok = scratch.data[i] == entry->data[i];
    ei::verify::Check("LoadSharedObject", true, ok, "id %d player %d, %d objects (original %d), %u bytes (original %u)",
                      entry->id, entry->player, entry->size, scratch.size, ms->position_ - start, originalEnd - start);
    for (int i = 0; i < scratch.size; ++i)
        if (scratch.data[i]) scratch.data[i]->Release();
    if (scratch.data) gamecrt::operator_delete(scratch.data);
}
EI_VERIFY(0x00589CA0, LoadSharedObjectVerify, 5, s_originalLoadShared);

// 005947E0: afterwards the list has one more entry, at its head, with the id, the value and one
// object per element of the source list.
static void __fastcall AddSharedObjectVerify(CWorldServer* self, void*, int id, int unused, std::uint32_t value,
                                             void* objects) {
    auto& list = *reinterpret_cast<mfc::CTypedPtrList<void>*>(reinterpret_cast<std::uint8_t*>(self) + 0x12E60);
    int before = list.count;
    int sourceCount = static_cast<mfc::CTypedPtrList<CEngineObject>*>(objects)->count;
    self->AddSharedObject(id, unused, value, objects);
    auto* head = reinterpret_cast<SharedEntry*>(reinterpret_cast<std::uint8_t*>(list.head) + 8);
    bool ok = list.count == before + 1 && head->id == id && head->value == value && head->size == sourceCount;
    ei::verify::Check("CWorldServer::AddSharedObject", true, ok, "id %d player %d, %d objects, list %d -> %d", id,
                      head->player, head->size, before, list.count);
}
EI_VERIFY(0x005947E0, AddSharedObjectVerify, 0, s_unused);

// 005956E0: refills units through original code; the map objects stay registered.
static void __fastcall PrepareSaveVerify(CWorldServer* self, void*) {
    self->PrepareSave();
    ei::verify::Check("CWorldServer::PrepareSave", true, MapObjectsRegistered(self), "%u map objects",
                      static_cast<unsigned>(ReachableObjects(self->mapObjects_)));
}
EI_VERIFY(0x005956E0, PrepareSaveVerify, 0, s_unused);
