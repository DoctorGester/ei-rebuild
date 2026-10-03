// Checks for CWorldServer::LoadMap (src/game/world_map.cpp). It loads files and objects, so only
// ours runs; on success the world's map and .mob names are the requested ones and +0x11D/+0x11E
// are set.
#include "verify.h"

#include "../../src/lib/crt.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;

bool SameName(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}
} // namespace

// 00595C70.
static int __fastcall LoadMapVerify(CWorldServer* self, void*, const char* map, const char* mob, void* graph, char fromSave) {
    int result = self->LoadMap(map, mob, graph, fromSave);
    auto* raw = reinterpret_cast<std::uint8_t*>(self);
    bool ok = result == 0 ||
              (result == 1 && SameName(self->mapName_, map) && SameName(self->mobName_, mob) && raw[0x11D] && raw[0x11E]);
    ei::verify::Check("CWorldServer::LoadMap", true, ok, "%s / %s save %d -> %d", map ? map : "", mob ? mob : "", fromSave,
                      result);
    return result;
}
EI_VERIFY(0x00595C70, LoadMapVerify, 0, s_unused);

// 0058B250: afterwards the id maps, the missile and counted lists are empty.
static void __fastcall ResetZoneVerify(CWorldServer* self, void*) {
    self->ResetZone();
    bool ok = self->objects_.map.Size() == 0 && self->mapObjects_.map.Size() == 0 && self->timedObjects_.map.Size() == 0 &&
              self->updateMap_.map.Size() == 0 && !self->projectiles_.list.head && self->projectiles_.list.count == 0 &&
              !self->counted_.head && self->counted_.count == 0 && self->exitCount_ == 0 && self->mobFiles_.size == 0;
    ei::verify::Check("CWorldServer::ResetZone", true, ok, "objects %u timed %u", static_cast<unsigned>(self->objects_.map.Size()),
                      static_cast<unsigned>(self->timedObjects_.map.Size()));
}
EI_VERIFY(0x0058B250, ResetZoneVerify, 0, s_unused);

// 005957E0: afterwards no player is left, the missile list and the timed objects are empty and the
// game kind is 0.
static void __fastcall TeardownZoneVerify(CWorldServer* self, void*) {
    self->TeardownZone();
    PlayerIterator it;
    bool noPlayers = it.First(self->players_) == nullptr;
    bool ok = noPlayers && !self->projectiles_.list.head && self->timedObjects_.map.Size() == 0 &&
              self->unknown12D64_ == 0 && self->gameResult_ == 0;
    ei::verify::Check("CWorldServer::TeardownZone", true, ok, "players gone %d, timed %u", noPlayers,
                      static_cast<unsigned>(self->timedObjects_.map.Size()));
}
EI_VERIFY(0x005957E0, TeardownZoneVerify, 0, s_unused);
