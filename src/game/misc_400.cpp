// Small pieces of the early code without a better home: the BaseManager subclass
// (its deleting destructor and the object factory differ), a
// cursor reset of the coder, an exception cleanup step and the aligned array allocations. See docs/notes/core.md.
#include "base_manager.h"
#include "link_figure.h"
#include "map_object_shadow.h"
#include "player_client.h"
#include "torch_client_spec.h"
#include "unit.h"
#include "unit_client_models.h"

#include <cstdint>
#include <cstring>
#include <new>

ClientSpecificManager::ClientSpecificManager() {}
ClientSpecificManager::~ClientSpecificManager() {}
const char* ClientSpecificManager::ClassName() { return "CClientSpecificManager"; }
ClassId ClientSpecificManager::GetClassId() { return ClassId::ClientSpecificManager; }

EngineObject* ClientSpecificManager::NewObject(int objectType) {
    switch (static_cast<SideObjectType>(objectType)) {
    case SideObjectType::Drawer: return new FigureMeshDrawer();
    case SideObjectType::Object3D: return new Object3DClientSpecific();
    case SideObjectType::MapObject: return new MapObjectClientSpecific();
    case SideObjectType::Unit: return new UnitClientSpecific();
    case SideObjectType::Player: return new PlayerClient();
    case SideObjectType::Torch: return new TorchObjectClientSpec();
    default: return nullptr;
    }
}
