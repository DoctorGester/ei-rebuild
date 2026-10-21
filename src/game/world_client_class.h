#pragma once

// The client world's terrain and text manager tables as compiled tags (C19, engine_object_class.h) in place of
// game.exe's 0073F6A8 (Terrain, with its tail 0073F6E8) and 0073F6F0 (CTextManager, the client's ClientTexts): each
// overrides the slots its game.exe table changes with the bodies that table reached. The client world's set-up
// (world_client_maps.cpp, which inlines both constructors) and the destructor bodies (world_destructors.cpp,
// text_sources.cpp) store them.

#include "item_db_text.h"
#include "terrain.h"
#include "world_class_slots.h"

#include "engine_object_class.h"

class TerrainClass final : public TransformObjectClass<TerrainClass, Terrain> {
public:
    int Create() override { return As<Terrain>()->CreateImpl(); }      // 2: 006C5430
    void Destroy() override { As<Terrain>()->DestroyImpl(); }          // 3: 006C54D0
    int RunTick() override { return As<Terrain>()->RunTickImpl(); }    // 6: 006CB1E0
    const char* ClassName() override { return TerrainClassName(As<Terrain>()); }   // 11: 00562B40
    int ClassId() override { return TerrainClassId(As<Terrain>()); }               // 12: 00562B50
    void CombineRotation(const Quaternion& parent) override { As<Terrain>()->CombineRotationImpl(parent); }   // 14: 006C6070
};

class CTextManagerClass final : public EngineObjectClass<CTextManagerClass, CTextManager> {
public:
    int Create() override { return As<CTextManager>()->Create(); }      // 2: 004DED20
    void Destroy() override { As<CTextManager>()->ClearSources(); }     // 3: 004DED40
    const char* ClassName() override { return TextManagerClassName(As<ClientTexts>()); }   // 11: 00562970
    int ClassId() override { return TextManagerClassId(As<ClientTexts>()); }               // 12: 00562980
};

inline void* Terrain::Vtable() { return TerrainClass::Vtable(); }
inline void* CTextManager::Vtable() { return CTextManagerClass::Vtable(); }
