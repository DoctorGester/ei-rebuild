#pragma once

// The side-specific managers: the factories of the objects' server and client halves (SideObjectType).
// ServerSpecificManager (class id 0x31) is created by the world server, ClientSpecificManager (0x30) by the client
// world. objects/side_managers.cpp. Names from the classes' own ClassName strings.

#include "engine/core/base_manager.h"
#include "engine/core/engine_object.h"

class ServerSpecificManager : public BaseManager {
public:
    ServerSpecificManager();
    ~ServerSpecificManager() override;

    // "ServerSpecificManager" (the exe's string).
    const char* ClassName() override;
    // 0x31.
    ClassId GetClassId() override;
    // A new object of a SideObjectType `type`: Object3D Object3DServer,
    // MapObject MapObjectServer (0x94), Unit UnitServer (0xB20), Player the player's server
    // object (0x5128), Torch TorchObjectSpec (constructed inline, then its Create);
    // null for any other type (Drawer and 0x4A too) or when the allocation fails.
    EngineObject* NewObject(int type) override;
};

class ClientSpecificManager : public BaseManager {
public:
    ClientSpecificManager();
    ~ClientSpecificManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    // A new object of a SideObjectType `type`, or null for any other type or a failed allocation.
    EngineObject* NewObject(int type) override;
};
