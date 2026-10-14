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
#include "originals_library.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_units.h"

namespace {

struct RawString {
    char* start;
    char* finish;
    char* eos;
};

// STLport string members: the end-of-storage member from an allocator and a value (RET 8),
// assign(first, last) (RET 8), the destructor.
void __fastcall StringEndOfStorage(char** a0, void*, const void* a2, const void* a3) { StringEndOfStorageOriginal((void*)a0, nullptr, a2, a3); }
// 005AA5F0: the manager's model of a name (figure_models.cpp).
// 004DBCA0: reads a length-prefixed string at *cursor into a CString and advances the cursor.
__declspec(naked) void __fastcall ReadName(mfc::CString*, const std::uint8_t**) EI_ORIGINAL(0x004DBCA0)
// The figure: link a piece to its parent (RET 0xC), finish.
__declspec(naked) void __fastcall LinkPiece(void*, void*, void*, const char*, const char*) EI_ORIGINAL(0x0050CC60)
__declspec(naked) void __fastcall FinishLinks(void*) EI_ORIGINAL(0x0050D2E0)
// 00480080: game::FatalError(format, ...).

// The figure's bone loaders (RET 4: {data, size}): 0x48-byte data, or any other size.
__declspec(naked) void __fastcall LoadBones48(void*, void*, void*) EI_ORIGINAL(0x0050BC00)
__declspec(naked) void __fastcall LoadBonesOther(void*, void*, void*) EI_ORIGINAL(0x0050B8C0)

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
    StringAssignSpan(&key, nullptr, name, name + std::strlen(name));
    void** model = static_cast<void**>(FigureManagerFindModelOriginal(manager, nullptr, &key));
    StringBufferFree(&key);

    std::uint32_t* data;
    if (model) {
        data = ei::VCall<std::uint32_t*>(model[0], kSlotPartRead, name, 0);
    } else {
        mfc::CString base(name);
        mfc::CString path = base + reinterpret_cast<const char*>(0x0078EC70);   // ".lnk"
        data = ei::VCall<std::uint32_t*>(manager, kSlotOpenResource, static_cast<const char*>(path), 0);
    }
    if (!data) return game::FatalError(reinterpret_cast<const char*>(0x00790D70), name);

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

// 005ACB80 (thiscall on the figure manager, RET 8): a figure's bones. The data comes from the
// cached model's bones part (+4, slot 5 with the figure's name at +0x1C and the size out) or from
// the file "<figure directory (+0x24)><figure name (slot 7)>.bon" (manager slot 19 with the size
// out); none returns 0. 0x48 bytes go to the figure's 0050BC00, other sizes to 0050B8C0, as
// {data, size}. The data is not released here. Returns 1.
int __fastcall LoadFigureBones(void* manager, void*, void** model, std::uint8_t* figure) {
    struct {
        void* data;
        int size;
    } bones{nullptr, 0};
    if (model) {
        bones.data = ei::VCall<void*>(model[1], kSlotPartRead, *reinterpret_cast<const char**>(figure + 0x1C),
                                      &bones.size);
    } else {
        mfc::CString name(ei::VCall<const char*>(figure, 7));
        mfc::CString directory(*reinterpret_cast<const mfc::CString*>(figure + 0x24));
        mfc::CString path = (directory + name) + reinterpret_cast<const char*>(0x00784640);   // ".bon"
        bones.data = ei::VCall<void*>(manager, kSlotOpenResource, static_cast<const char*>(path), &bones.size);
    }
    if (!bones.data) return 0;
    if (bones.size == 0x48) LoadBones48(figure, nullptr, &bones);
    else LoadBonesOther(figure, nullptr, &bones);
    return 1;
}
EI_REPLACE(0x005ACB80, LoadFigureBones);
