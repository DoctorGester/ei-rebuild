#pragma once

#include "engine_object.h"
#include "lib/stlp.h"

#include <cstddef>

// Base of every engine manager (textures, figures, objects, ...): owns objects and finds them by id.
// vtable 0073B870 (25 slots; slots 0-12 as CEngineObject). Name from the game's own error strings
// ("CBaseManager::RegisterInstant"). Layout recovered only as far as the members used so far.
class CBaseManager : public CEngineObject {
public:
    // 00403F40. Object registered under `id`, or null.
    CEngineObject* FindObject(unsigned id) const;
    // 00402B20. The constructor body (base_manager_core.cpp); returns this.
    CBaseManager* ConstructBase();
    // 00402DF0. The destructor body: Destroy (slot 3, 004031E0), the containers, the engine object.
    void DestructBody();
    // 004031E0 (slot 3). Destroys, releases and deletes every object and clears the tables.
    void Destroy() override;

    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash> objects_;   // +0x1C by id
    int maxId_;                                                         // +0x30 highest id handed out
    std::uint8_t unknown_34[0xAC - 0x34];                               // MFC CPtrList at +0x34, more maps
    void* owner_AC;                                                     // +0xAC set by Init (slot 13)
};
static_assert(offsetof(CBaseManager, objects_) == 0x1C);
static_assert(sizeof(CBaseManager) == 0xB0);
