// Small pieces of the early code without a better home: the CBaseManager subclass
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

CClientSpecificManager::CClientSpecificManager() {}
CClientSpecificManager::~CClientSpecificManager() {}
const char* CClientSpecificManager::ClassName() { return "CClientSpecificManager"; }
ClassId CClientSpecificManager::GetClassId() { return kClassClientSpecificManager; }

CEngineObject* CClientSpecificManager::NewObject(int type) {
    switch (type) {
    case 'E': return new FigureMeshDrawer();
    case 'F': return new CObject3DClientSpecific();
    case 'G': return new CMapObjectClientSpecific();
    case 'H': return new CUnitClientSpecific();
    case 'I': return new CPlayerClient();
    case 'K': return new CTorchObjectClientSpec();
    default: return nullptr;
    }
}
