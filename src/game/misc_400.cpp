// Small pieces of 00400000-00440000 without a better home: the CBaseManager subclass 0073BECC
// (constructed by 005620D9; its deleting destructor and slot 14, the object factory, differ), a
// cursor reset of the coder, an exception cleanup step and the aligned array allocations. See docs/notes/core.md.
#include "base_manager.h"
#include "link_figure.h"
#include "net_coder.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace misc400 {

constexpr std::uintptr_t kManagerVtable = 0x0073BECC;

// The constructors of the objects 0042D700 makes (outside this range; each returns this). Named by
// the vtable each one sets.
// 005A7010 (rewritten, figure_drawer.cpp): the figure drawer, CFigureClientSpecific.
void* __fastcall Construct73FB10(void* memory) { return static_cast<FigureMeshDrawer*>(memory)->Construct(); }
__declspec(naked) void* __fastcall Construct73DE90(void* /*memory*/) EI_ORIGINAL(0x0051AB00)
__declspec(naked) void* __fastcall ConstructMapObjectClientSpecific(void* /*memory*/) EI_ORIGINAL(0x00510450)
__declspec(naked) void* __fastcall Construct73ECD8(void* /*memory*/) EI_ORIGINAL(0x00533AD0)
__declspec(naked) void* __fastcall Construct743180(void* /*memory*/) EI_ORIGINAL(0x00666D10)
__declspec(naked) void* __fastcall Construct73E954(void* /*memory*/) EI_ORIGINAL(0x0052D0D0)

CEngineObject* New(std::size_t size, void* (__fastcall* construct)(void*)) {
    void* memory = gamecrt::operator_new(size);
    return memory ? static_cast<CEngineObject*>(construct(memory)) : nullptr;
}

// 0042D690: the base manager constructed, then this class's vtable. Returns this.
CBaseManager* __fastcall ManagerConstruct(CBaseManager* self) {
    self->ConstructBase();
    std::memcpy(self, &kManagerVtable, 4);
    return self;
}

// 0042D6F0: the destructor body: this class's vtable, then the base manager's body (a tail call).
void __fastcall ManagerDestruct(CBaseManager* self) {
    std::memcpy(self, &kManagerVtable, 4);
    self->DestructBody();
}

// 0042D6B0 / 0042D6C0: the class name ("CClientSpecificManager", 00783DD8) and class id (0x30).
const char* __fastcall ManagerClassName(const CBaseManager*) { return reinterpret_cast<const char*>(0x00783DD8); }
int __fastcall ManagerClassId(const CBaseManager*) { return 0x30; }

} // namespace misc400

// 0042D700 (slot 14): a new object of `type` ('E' .. 'K'), or null for 'J', any other type or a
// failed allocation.
CEngineObject* CClientSpecificManager::NewObjectImpl(int type) {
    switch (type) {
    case 'E': return misc400::New(0x3C, misc400::Construct73FB10);
    case 'F': return misc400::New(0xFC, misc400::Construct73DE90);
    case 'G': return misc400::New(0x58, misc400::ConstructMapObjectClientSpecific);
    case 'H': return misc400::New(0xDC, misc400::Construct73ECD8);
    case 'I': return misc400::New(0x50F8, misc400::Construct743180);
    case 'K': return misc400::New(0x64, misc400::Construct73E954);
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

EI_REPLACE(0x0042D690, misc400::ManagerConstruct);
EI_REPLACE(0x0042D6F0, misc400::ManagerDestruct);
EI_REPLACE(0x0042D6B0, misc400::ManagerClassName);
EI_REPLACE(0x0042D6C0, misc400::ManagerClassId);
EI_REPLACE_METHOD(0x0042D700, CClientSpecificManager::NewObjectImpl);
EI_REPLACE(0x00406B30, misc400::AlignedWords);
EI_REPLACE(0x00406B50, misc400::AlignedDwords);
EI_REPLACE_METHOD(0x00430FC0, BufferCursor::Start);
EI_REPLACE(0x00433F10, misc400::ClearPointee);
