// 005AC880 (thiscall on the figure manager, RET 8): links a figure's pieces from the skeleton file
// of the model `name`. With the model cached by 005AA5F0 its mesh part reads the links (slot 5,
// freed by slot 6); without it the manager opens "<name>.lnk" (slot 19, freed by slot 20); with
// neither the fatal error "Can't load skeleton file" is returned. The data is a count and that
// many (child, parent) length-prefixed names (read by 004DBCA0 into CStrings), each passed to
// the figure's 0050CC60 with the model; 0050D2E0 finishes the figure. Returns 1. See
// docs/notes/models.md.
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

struct RawString {
    char* start;
    char* finish;
    char* eos;
};

// STLport string members: the end-of-storage member from an allocator and a value (RET 8),
// assign(first, last) (RET 8), the destructor.
__declspec(naked) void __fastcall StringEndOfStorage(char**, void*, const void*, const void*) EI_ORIGINAL(0x00425080)
__declspec(naked) void __fastcall StringAssign(RawString*, void*, const char*, const char*) EI_ORIGINAL(0x00428F10)
__declspec(naked) void __fastcall StringDestroy(RawString*) EI_ORIGINAL(0x00425040)
// 005AA5F0: the manager's model of a name (figure_models.cpp).
__declspec(naked) void** __fastcall FindModelAt(void*, void*, const RawString*) EI_ORIGINAL(0x005AA5F0)
// 004DBCA0: reads a length-prefixed string at *cursor into a CString and advances the cursor.
__declspec(naked) void __fastcall ReadName(mfc::CString*, const std::uint8_t**) EI_ORIGINAL(0x004DBCA0)
// The figure: link a piece to its parent (RET 0xC), finish.
__declspec(naked) void __fastcall LinkPiece(void*, void*, void*, const char*, const char*) EI_ORIGINAL(0x0050CC60)
__declspec(naked) void __fastcall FinishLinks(void*) EI_ORIGINAL(0x0050D2E0)
// 00480080: FatalError(format, ...).
__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)

constexpr int kSlotOpenResource = 19;
constexpr int kSlotFreeResource = 20;
constexpr int kSlotPartRead = 5;
constexpr int kSlotPartFree = 6;

} // namespace

int __fastcall LoadSkeletonLinks(void* manager, void*, void* figure, const char* name) {
    RawString key;
    key.start = nullptr;
    key.finish = nullptr;
    char allocatorTag;
    int zero = 0;
    StringEndOfStorage(&key.eos, nullptr, &allocatorTag, &zero);
    StringAssign(&key, nullptr, name, name + std::strlen(name));
    void** model = FindModelAt(manager, nullptr, &key);
    StringDestroy(&key);

    std::uint32_t* data;
    if (model) {
        data = ei::VCall<std::uint32_t*>(model[0], kSlotPartRead, name, 0);
    } else {
        mfc::CString base(name);
        mfc::CString path = base + reinterpret_cast<const char*>(0x0078EC70);   // ".lnk"
        data = ei::VCall<std::uint32_t*>(manager, kSlotOpenResource, static_cast<const char*>(path), 0);
    }
    if (!data) return FatalError(reinterpret_cast<const char*>(0x00790D70), name);

    const std::uint8_t* cursor = reinterpret_cast<const std::uint8_t*>(data + 1);
    {
        mfc::CString child, parent;
        for (std::uint32_t i = data[0]; i != 0; --i) {
            ReadName(&child, &cursor);
            ReadName(&parent, &cursor);
            LinkPiece(figure, nullptr, model, child, parent);
        }
        FinishLinks(figure);
        if (model) ei::VCall<void>(model[0], kSlotPartFree, data);
        else ei::VCall<void>(manager, kSlotFreeResource, data);
    }
    return 1;
}

EI_REPLACE(0x005AC880, LoadSkeletonLinks);
