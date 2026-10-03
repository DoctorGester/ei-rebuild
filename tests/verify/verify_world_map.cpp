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
