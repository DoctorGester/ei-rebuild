// Small pieces of the early code without a better home: the CBaseManager subclass
// (its deleting destructor and slot 14, the object factory, differ), a
// cursor reset of the coder, an exception cleanup step and the aligned array allocations. See docs/notes/core.md.
#include "torch_client_spec.h"   // before windows.h (GetClassName)
#include "base_manager.h"
#include "link_figure.h"
#include "map_object_shadow.h"
#include "net_coder.h"
#include "player_client.h"
#include "unit.h"
#include "unit_client_models.h"

#include "ei/hook.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace misc400 {


// The destructor.
void __fastcall ManagerDestruct(CBaseManager* self) {
    auto* manager = static_cast<CClientSpecificManager*>(self);
    manager->CClientSpecificManager::~CClientSpecificManager();
}

} // namespace misc400

CClientSpecificManager::CClientSpecificManager() {}
CClientSpecificManager::~CClientSpecificManager() {}
const char* CClientSpecificManager::GetClassName() { return "CClientSpecificManager"; }
int CClientSpecificManager::GetClassId() { return 0x30; }

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

EI_REWRITTEN_SPECIAL(0x0042D690, CClientSpecificManager::CClientSpecificManager);
EI_REWRITTEN(0x0042D6F0, misc400::ManagerDestruct);
EI_REWRITTEN(0x0042D6B0, CClientSpecificManager::GetClassName);
EI_REWRITTEN(0x0042D6C0, CClientSpecificManager::GetClassId);
EI_REWRITTEN(0x0042D700, CClientSpecificManager::NewObject);
