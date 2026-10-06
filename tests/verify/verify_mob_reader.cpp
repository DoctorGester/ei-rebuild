// Checks for the .mob reader (mob_reader.cpp). Both run ours only: they read the file (its
// position moves on), allocate the records and change the server (time, wind, world objects).
// Scenarios: every zone load (newgame, world_load, zone_gz2g_traps, zone_base_bz1g).
#include "verify.h"

#include "../../src/game/map_spec.h"

#include <algorithm>

// 00481F00. Afterwards every record is filed under its own id, the count matches the nodes and
// the largest id is the larger of the one before and the records' largest.
static bool (__fastcall* s_readMobChunks)(Chunk*, void*, mapspec::MobRecords*, mapspec::ZoneDiplomacy*, mfc::CString*);
static bool __fastcall ReadMobChunksVerify(Chunk* root, void*, mapspec::MobRecords* records,
                                           mapspec::ZoneDiplomacy* diplomacy, mfc::CString* scripts) {
    const std::int32_t maxBefore = records->maxId;
    bool result = mapspec::ReadMobChunks(root, nullptr, records, diplomacy, scripts);
    std::size_t nodes = 0;
    std::int32_t largest = maxBefore;
    bool keyed = true;
    records->byId.ForEach([&](const auto* node) {
        ++nodes;
        if (!node->value || node->value->id != node->key) keyed = false;
        largest = std::max(largest, node->key);
    });
    ei::verify::Check("ReadMobChunks keys", true, keyed, "every record under its id");
    ei::verify::Check("ReadMobChunks count", records->byId.Size(), nodes, "%u");
    ei::verify::Check("ReadMobChunks maxId", largest, records->maxId, "%d");
    ei::verify::Check("ReadMobChunks file read", root->size_, root->consumed_, "%d");
    return result;
}
EI_VERIFY(0x00481F00, ReadMobChunksVerify, 0, s_readMobChunks);

// 00481660. Returns true; the file is read to its end.
static bool (__fastcall* s_loadMobFile)(Chunk*, void*, void*, int);
static bool __fastcall LoadMobFileVerify(Chunk* root, void*, void* unused, int keepIds) {
    bool result = mapspec::LoadMobFile(root, nullptr, unused, keepIds);
    ei::verify::Check("LoadMobFile result", true, result, "%d");
    ei::verify::Check("LoadMobFile file read", root->size_, root->consumed_, "%d");
    return result;
}
EI_VERIFY(0x00481660, LoadMobFileVerify, 0, s_loadMobFile);
