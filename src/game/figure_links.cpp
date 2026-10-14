// 005AC880 (thiscall on the figure manager, RET 8): links a figure's pieces from the skeleton file
// of the model `name`. With the model cached by 005AA5F0 its mesh archive reads the links (GetFile,
// freed by CloseFile); without it the manager opens "<name>.lnk" (its GetFile / CloseFile); with
// neither the fatal error "Can't load skeleton file" is returned. The data is a count and that
// many (child, parent) length-prefixed names (read by 004DBCA0 into CStrings), each passed to
// the animation state's LinkPiece (0050CC60) with the model; FindRoot (0050D2E0) finishes it.
// Returns 1. See docs/notes/models.md.
#include "figure_manager.h"       // CFigureManager, FigureModel
#include "map_object_figures.h"   // CFigureAnimationState, FigureBone

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_units.h"

namespace {

// An STLport string the game's own members build and free (start, finish, end of storage).
struct StringSlot {
    char* start;
    char* finish;
    char* eos;
};
static_assert(sizeof(StringSlot) == sizeof(stlp::string));

// STLport string members: the end-of-storage member from an allocator and a value (RET 8),
// assign(first, last) (RET 8), the destructor.
void __fastcall StringEndOfStorage(char** a0, void*, const void* a2, const void* a3) { StringEndOfStorageOriginal((void*)a0, nullptr, a2, a3); }
// 005AA5F0: the manager's model of a name (figure_models.cpp).
// 004DBCA0: reads a length-prefixed string at *cursor into a CString and advances the cursor.
__declspec(naked) void __fastcall ReadName(mfc::CString*, const std::uint8_t**) EI_ORIGINAL(0x004DBCA0)
// The animation state: LinkPiece (RET 0xC), FindRoot (map_object_animation_bones.cpp).
__declspec(naked) void __fastcall LinkPiece(CFigureAnimationState*, void*, FigureModel*, const char*, const char*)
    EI_ORIGINAL(0x0050CC60)
__declspec(naked) void __fastcall FinishLinks(CFigureAnimationState*) EI_ORIGINAL(0x0050D2E0)
// 00480080: game::FatalError(format, ...).

// The bone's cage interpolation (map_object_cage.h; RET 4: {data, size}): 0x48-byte data (a prism of
// six points), or any other size (a cube of eight).
__declspec(naked) void __fastcall LoadBones48(FigureBone*, void*, void*) EI_ORIGINAL(0x0050BC00)
__declspec(naked) void __fastcall LoadBonesOther(FigureBone*, void*, void*) EI_ORIGINAL(0x0050B8C0)

} // namespace

int CFigureManager::LoadSkeletonLinks(CFigureAnimationState* state, const char* name) {
    StringSlot key;
    key.start = nullptr;
    key.finish = nullptr;
    char allocatorTag;
    int zero = 0;
    StringEndOfStorage(&key.eos, nullptr, &allocatorTag, &zero);
    StringAssignSpan(&key, nullptr, name, name + std::strlen(name));
    auto* model = static_cast<FigureModel*>(FigureManagerFindModelOriginal(this, nullptr, &key));
    StringBufferFree(&key);

    std::uint32_t* data;
    if (model) {
        data = static_cast<std::uint32_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        mfc::CString base(name);
        mfc::CString path = base + reinterpret_cast<const char*>(0x0078EC70);   // ".lnk"
        data = static_cast<std::uint32_t*>(GetFile(path, nullptr));
    }
    if (!data) return game::FatalError(reinterpret_cast<const char*>(0x00790D70), name);

    const std::uint8_t* cursor = reinterpret_cast<const std::uint8_t*>(data + 1);
    {
        mfc::CString child, parent;
        for (std::uint32_t i = data[0]; i != 0; --i) {
            ReadName(&child, &cursor);
            ReadName(&parent, &cursor);
            LinkPiece(state, nullptr, model, child, parent);
        }
        FinishLinks(state);
        if (model) model->mesh->CloseFile(data);
        else CloseFile(data);
    }
    return 1;
}

EI_REPLACE_METHOD(0x005AC880, CFigureManager::LoadSkeletonLinks);

// 005ACB80 (thiscall on the figure manager, RET 8): a bone's cage. The data comes from the cached
// model's bones archive (GetFile with the bone's name and the size out) or from the file
// "<bone's model name (name3)><bone's name (slot 7)>.bon" (the manager's GetFile with the size
// out); none returns 0. 0x48 bytes go to the bone's 0050BC00, other sizes to 0050B8C0, as
// {data, size}. The data is not released here. Returns 1.
int CFigureManager::LoadFigureBones(FigureModel* model, FigureBone* bone) {
    struct {
        void* data;
        std::uint32_t size;
    } bones{nullptr, 0};
    if (model) {
        bones.data = model->bones->GetFile(bone->name, &bones.size);
    } else {
        mfc::CString name(bone->GetName());
        mfc::CString directory(bone->name3);
        mfc::CString path = (directory + name) + reinterpret_cast<const char*>(0x00784640);   // ".bon"
        bones.data = GetFile(path, &bones.size);
    }
    if (!bones.data) return 0;
    if (bones.size == 0x48) LoadBones48(bone, nullptr, &bones);
    else LoadBonesOther(bone, nullptr, &bones);
    return 1;
}
EI_REPLACE_METHOD(0x005ACB80, CFigureManager::LoadFigureBones);
