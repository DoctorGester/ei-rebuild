#pragma once

// The worlds' tables as compiled tags (C19, engine_object_class.h) in place of game.exe's 0073F18C (CWorld, the
// base: ConstructBase and the base destructor store it), 0073F41C (CWorldClient) and 0073F928 (CWorldServer; their
// constructors in world_construct.cpp and destructors in world_destructors.cpp store them). WorldClass declares
// CWorld's slots 13-41 (world.h) with the base's bodies; each world overrides the slots its game.exe table changes
// with the bodies that table reached (qualified: no dispatch). The getters 13-23 and the constant slots are the
// generated short functions' bodies (short_functions.cpp keeps their registrations).

#include "party.h"
#include "world.h"
#include "world_class_slots.h"

#include "engine_object_class.h"

template <typename Self, typename T>
class WorldClass : public EngineObjectClass<Self, T> {
public:
    int Create() override { return WorldCreateManagers(World()); }   // 2: 0055EE40
    void Destroy() override { World()->DestroyManagers(); }          // 3: 0055F8E0
    int RunTick() override { return WorldBaseRunTick(World()); }     // 6: 0055FAD0 (1)

    virtual CWorld* ThisWorld() { return World(); }                                     // 13: 0055EB40
    virtual CBaseManager* TimedObjectManager() { return World()->manager48_; }          // 14: 0055EB90
    virtual CBaseManager* FigureManager() { return World()->manager3C_; }               // 15: 0055EB60
    virtual CEngineObject* ItemConstructor() { return World()->manager58_; }            // 16: 0055EBB0
    virtual CEngineObject* SpellConstructor() { return World()->manager50_; }           // 17: 0055EBC0
    virtual CItemManager* ItemFactory() { return World()->itemManager_; }               // 18: 004BA000
    virtual CBaseManager* MapObjectManager() { return World()->manager40_; }            // 19: 0055EB70
    virtual CBaseManager* PlayerList() { return World()->players_; }                    // 20: 0055EB50
    virtual CBaseManager* SpecificManager() { return World()->manager44_; }             // 21: 0055EB80
    virtual CBaseManager* SpellManager() { return World()->manager4C_; }                // 22: 0055EBA0
    virtual CLinkFigureLoader* FigureLoader() { return World()->figureLoader_; }        // 23: 0055EBD0
    // 24: 0055E940, CParty's copy of an item (the world's slots 18 and 22 are the getters it calls).
    virtual CItem* DuplicateItem(CItem* item) { return this->template As<CParty>()->CParty::MakeCopyOf(item); }
    virtual const char* TexturePath() { return nullptr; }   // 25: 0055EB20
    virtual int IsServer() { return 0; }                    // 26: 0055EB30
    virtual double GridHeightAt(float, float) { return _purecall(); }         // 27: pure
    virtual double RawSurfaceHeightAt(float, float) { return _purecall(); }   // 28: pure
    virtual double GroundHeightAt(float, float) { return _purecall(); }       // 29: pure
    virtual double SurfaceHeightAt(float, float) { return _purecall(); }      // 30: pure
    virtual void SetTime(float hours) { World()->CWorld::SetTimeImpl(hours); }              // 31: 0055FB30
    virtual void SetTimeSpeed(float speed) { World()->CWorld::SetTimeSpeedImpl(speed); }    // 32: 0055FB50
    virtual CMaterialManager* MaterialManager() { return nullptr; }    // 33: 0055EBE0
    virtual CTextureManager* TextureManager() { return nullptr; }      // 34: 0055EBF0
    virtual CBaseManager* ClientLightManager() { return nullptr; }     // 35: 0055EC00
    virtual CParticleManager* ClientParticleManager() { return nullptr; }   // 36: 0055EC10
    virtual CEngineObject* ClientManager194() { return nullptr; }      // 37: 0055EC20
    virtual CEngineObject* ClientManager198() { return nullptr; }      // 38: 0055EC30
    virtual int Unused39() { return 0; }                               // 39: 0055EC40
    virtual CMapObject* ObjectWithId(int id) { return World()->CWorld::FindMapObject(id); }   // 40: 0055FAE0
    virtual void MoveObject(CEngineObject*, const float*) {}           // 41: 0055EC50 (RET 8)

protected:
    CWorld* World() { return this->template As<CWorld>(); }
};

class CWorldBaseClass final : public WorldClass<CWorldBaseClass, CWorld> {};

class CWorldClientClass final : public WorldClass<CWorldClientClass, CWorldClient> {
public:
    int Create() override { return Client()->Setup(); }       // 2: 00561E50
    void Destroy() override { Client()->Teardown(); }         // 3: 00563290
    int RunTick() override { return Client()->Segment(); }    // 6: 005641F0
    const char* ClassName() override { return WorldClientClassName(Client()); }   // 11: 005619E0
    int ClassId() override { return WorldClientClassId(Client()); }               // 12: 005619F0
    const char* TexturePath() override { return WorldClientTexturePath(Client()); }   // 25: 00561890
    double GridHeightAt(float x, float y) override { return Client()->ClampedGroundHeight(x, y); }           // 27: 00563F70
    double RawSurfaceHeightAt(float x, float y) override { return Client()->ClientRawSurfaceHeight(x, y); }   // 28: 00564070
    double GroundHeightAt(float x, float y) override { return Client()->ClientGroundHeight(x, y); }          // 29: 005618E0
    double SurfaceHeightAt(float x, float y) override { return Client()->ClientSurfaceHeight(x, y); }        // 30: 00561930
    CMaterialManager* MaterialManager() override { return WorldClientMaterialManager(Client()); }             // 33: 00561980
    CTextureManager* TextureManager() override { return WorldClientTextureManager(Client()); }    // 34: 00561990
    CBaseManager* ClientLightManager() override { return WorldClientLightManager(Client()); }     // 35: 005619A0
    CParticleManager* ClientParticleManager() override { return WorldClientParticleManager(Client()); }   // 36: 005619B0
    CEngineObject* ClientManager194() override { return WorldClientManager194(Client()); }       // 37: 005619C0
    CEngineObject* ClientManager198() override { return WorldClientManager198(Client()); }       // 38: 005619D0
    void MoveObject(CEngineObject* object, const float* position) override {   // 41: 005618A0
        Client()->MoveObjectImpl(object, position);
    }
    virtual void MoveObject2(CEngineObject* object, const float* position) {   // 42: 005618C0
        Client()->MoveObject2Impl(object, position);
    }

private:
    CWorldClient* Client() { return As<CWorldClient>(); }
};

class CWorldServerClass final : public WorldClass<CWorldServerClass, CWorldServer> {
public:
    int Create() override { return Server()->Setup(); }      // 2: 0058AE10
    void Destroy() override { Server()->Teardown(); }        // 3: 0058B020
    int RunTick() override { return Server()->Update(); }    // 6: 0058DFF0
    const char* ClassName() override { return WorldServerClassName(Server()); }   // 11: 0058A410
    int ClassId() override { return WorldServerClassId(Server()); }               // 12: 0058A420
    CItem* DuplicateItem(CItem* item) override { return Server()->CopyItem(item); }   // 24: 00594CE0
    int IsServer() override { return WorldServerIsServer(Server()); }                 // 26: 0058A380
    double GridHeightAt(float x, float y) override { return Server()->GridHeight(x, y); }                    // 27: 00594220
    double RawSurfaceHeightAt(float x, float y) override { return Server()->ServerRawSurfaceHeight(x, y); }  // 28: 0058A390
    double GroundHeightAt(float x, float y) override { return Server()->ServerGroundHeight(x, y); }          // 29: 0058A3D0
    double SurfaceHeightAt(float x, float y) override { return Server()->ServerSurfaceHeight(x, y); }        // 30: 0058A3F0
    void SetTime(float hours) override { Server()->CWorldServer::SetTimeImpl(hours); }             // 31: 0058EEA0
    void SetTimeSpeed(float speed) override { Server()->CWorldServer::SetTimeSpeedImpl(speed); }   // 32: 0058EEE0
    CMapObject* ObjectWithId(int id) override { return Server()->FindObject(static_cast<unsigned>(id)); }   // 40: 00593AA0

private:
    CWorldServer* Server() { return As<CWorldServer>(); }
};

inline void* CWorld::BaseVtable() { return CWorldBaseClass::Vtable(); }
inline void* CWorldClient::Vtable() { return CWorldClientClass::Vtable(); }
inline void* CWorldServer::Vtable() { return CWorldServerClass::Vtable(); }
