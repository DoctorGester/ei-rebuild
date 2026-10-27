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

// Slots 11 and 12.

} // namespace misc400

CClientSpecificManager::CClientSpecificManager() {}
CClientSpecificManager::~CClientSpecificManager() {}
const char* CClientSpecificManager::GetClassName() { return "CClientSpecificManager"; }
int CClientSpecificManager::GetClassId() { return 0x30; }

// A new object of `type` ('E' .. 'K'), or null for 'J', any other type or a failed allocation. The
// constructors (the figure drawer and five other classes') each return this.
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

namespace misc400 {

// Memory for `count` words / dwords plus 0x24 bytes, the start aligned to 32 bytes with the block's address
// in the dword before it (vertex and index arrays; freed through that address).
void* __fastcall AlignedWords(int count) { return lib::AllocAligned32(static_cast<std::size_t>(count * 2)); }
void* __fastcall AlignedDwords(int count) { return lib::AllocAligned32(static_cast<std::size_t>(count * 4)); }

// An exception cleanup step: the pointer ECX points at is cleared where it points.
void __fastcall ClearPointee(void** self) { *static_cast<void**>(*self) = nullptr; }

} // namespace misc400

// A cursor started at `p` (no byte reserved, no pending bits; the accumulator is left
// alone). The two other arguments are not used. Returns 0.
std::uint32_t BufferCursor::Start(std::uint8_t* first, std::uint32_t, std::uint32_t) {
    p = first;
    bits = 0;
    bytePtr = nullptr;
    return 0;
}

EI_REWRITTEN_SPECIAL(0x0042D690, CClientSpecificManager::CClientSpecificManager);
EI_REWRITTEN(0x0042D6F0, misc400::ManagerDestruct);
EI_REWRITTEN(0x0042D6B0, CClientSpecificManager::GetClassName);
EI_REWRITTEN(0x0042D6C0, CClientSpecificManager::GetClassId);
EI_REWRITTEN(0x0042D700, CClientSpecificManager::NewObject);
EI_REWRITTEN(0x00406B30, misc400::AlignedWords);
EI_REWRITTEN(0x00406B50, misc400::AlignedDwords);
EI_REWRITTEN(0x00430FC0, BufferCursor::Start);
EI_REWRITTEN(0x00433F10, misc400::ClearPointee);
