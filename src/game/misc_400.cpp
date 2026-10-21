// Small pieces of 00400000-00440000 without a better home: the CBaseManager subclass 0073BECC
// (constructed by 005620D9; its deleting destructor and slot 14, the object factory, differ), a
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
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace misc400 {


CEngineObject* New(std::size_t size, void* (__fastcall* construct)(void*)) {
    void* memory = gamecrt::operator_new(size);
    return memory ? static_cast<CEngineObject*>(construct(memory)) : nullptr;
}


// 0042D6F0: the destructor.
void __fastcall ManagerDestruct(CBaseManager* self) {
    auto* manager = static_cast<CClientSpecificManager*>(self);
    manager->CClientSpecificManager::~CClientSpecificManager();
}

// 0042D6B0 / 0042D6C0: slots 11 and 12.
const char* __fastcall ManagerClassName(CClientSpecificManager* self) { return self->CClientSpecificManager::GetClassName(); }
int __fastcall ManagerClassId(CClientSpecificManager* self) { return self->CClientSpecificManager::GetClassId(); }

} // namespace misc400

CClientSpecificManager::CClientSpecificManager() {}
CClientSpecificManager::~CClientSpecificManager() {}
CClientSpecificManager* CClientSpecificManager::Construct() { return new (this) CClientSpecificManager(); }
const char* CClientSpecificManager::GetClassName() { return "CClientSpecificManager"; }
int CClientSpecificManager::GetClassId() { return 0x30; }
CEngineObject* CClientSpecificManager::NewObject(int type) { return NewObjectImpl(type); }

// 0042D700 (slot 14): a new object of `type` ('E' .. 'K'), or null for 'J', any other type or a
// failed allocation. The constructors (005A7010 the figure drawer, 0051AB00, 00510450, 00533AD0,
// 00666D10, 0052D0D0) each return this.
CEngineObject* CClientSpecificManager::NewObjectImpl(int type) {
    switch (type) {
    case 'E': return misc400::New(0x3C, ConstructOn<FigureMeshDrawer, &FigureMeshDrawer::Construct>);
    case 'F': return misc400::New(0xFC, ConstructOn<CObject3DClientSpecific, &CObject3DClientSpecific::Construct>);
    case 'G': return misc400::New(0x58, ConstructOn<CMapObjectClientSpecific, &CMapObjectClientSpecific::Construct>);
    case 'H': return misc400::New(0xDC, ConstructOn<CUnitClientSpecific, &CUnitClientSpecific::ConstructClient>);
    case 'I': return misc400::New(0x50F8, ConstructOn<CPlayerClient, &CPlayerClient::Construct>);
    case 'K': return misc400::New(0x64, ConstructOn<CTorchObjectClientSpec, &CTorchObjectClientSpec::Construct>);
    default: return nullptr;
    }
}

namespace misc400 {

// 00406B30 / 00406B50: memory for `count` words / dwords plus 0x24 bytes, the start aligned to 32
// bytes with the block's address in the dword before it (vertex and index arrays; freed through
// that address).
void* AlignedNew(std::size_t bytes) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
    auto* start = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    std::memcpy(start - 4, &block, 4);
    return start;
}
void* __fastcall AlignedWords(int count) { return AlignedNew(static_cast<std::size_t>(count * 2 + 0x24)); }
void* __fastcall AlignedDwords(int count) { return AlignedNew(static_cast<std::size_t>(count * 4 + 0x24)); }

// 00433F10: an exception cleanup step: the pointer ECX points at is cleared where it points.
void __fastcall ClearPointee(void** self) { *static_cast<void**>(*self) = nullptr; }

} // namespace misc400

// 00430FC0: a cursor started at `p` (no byte reserved, no pending bits; the accumulator is left
// alone). The two other arguments are not used. Returns 0.
std::uint32_t BufferCursor::Start(std::uint8_t* first, std::uint32_t, std::uint32_t) {
    p = first;
    bits = 0;
    bytePtr = nullptr;
    return 0;
}

EI_REWRITTEN(0x0042D690, CClientSpecificManager::Construct);
EI_REWRITTEN(0x0042D6F0, misc400::ManagerDestruct);
EI_REPLACE(0x0042D6B0, misc400::ManagerClassName);
EI_REPLACE(0x0042D6C0, misc400::ManagerClassId);
EI_REWRITTEN(0x0042D700, CClientSpecificManager::NewObjectImpl);
EI_REWRITTEN(0x00406B30, misc400::AlignedWords);
EI_REWRITTEN(0x00406B50, misc400::AlignedDwords);
