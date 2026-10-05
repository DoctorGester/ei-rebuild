// 005ACCF0 (thiscall on the figure manager, RET 0xC): sets a figure's animation by name. Without
// an animation name the figure's animation is cleared (0050D950, whose EAX is returned). Otherwise
// the model is looked up (005AA5F0) and its animation part asked for the name (005A9EF0); each
// failure logs "Can't open model" / "Can't open animation ... for model" (0044C470) and returns
// 0; a found animation goes to the figure (0050DA10, whose EAX is returned). The temporary
// STLport strings are built and freed through the original's own members. See
// docs/notes/models.md.
#include "ei/hook.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "originals_engine.h"

namespace {

struct RawString {
    char* start;
    char* finish;
    char* eos;
};

// STLport string members: the end-of-storage member from an allocator and a value (RET 8), range
// initialisation (first, last, tag; RET 0xC), the allocator's deallocate (p, n; RET 8) and the
// node allocator's deallocate (ECX p, EDX n).
void __fastcall StringEndOfStorage(char** a0, void*, const void* a2, const void* a3) { StringEndOfStorageOriginal((void*)a0, nullptr, a2, a3); }
inline void __fastcall StringInitRange(RawString* a0, void*, const char* a2, const char* a3, const void* a4) { StringInitRangeOriginal((void*)a0, nullptr, a2, a3, a4); }
inline void __fastcall StringDeallocate(char**, void*, char* a2, int a3) { StringDeallocateOriginal(a2, (std::size_t)a3); }
void __fastcall NodeDeallocate(char* a0, int a1) { NodeDeallocateOriginal((void*)a0, (std::size_t)a1); }
// 005AA5F0: the manager's model of a name; 005A9EF0 (RET 4): the model's animation of a name.
__declspec(naked) void* __fastcall FindModelAt(void*, void*, const RawString*) EI_ORIGINAL(0x005AA5F0)
__declspec(naked) void* __fastcall FindAnimation(void*, void*, const RawString*) EI_ORIGINAL(0x005A9EF0)
// The figure: clear its animation; set an animation (RET 8: animation, name).
__declspec(naked) int __fastcall ClearAnimation(void*) EI_ORIGINAL(0x0050D950)
__declspec(naked) int __fastcall SetAnimation(void*, void*, void*, const char*) EI_ORIGINAL(0x0050DA10)
// 0044C470: the game's debug message (cdecl, printf-style).

void MakeString(RawString& s, const char* text) {
    s.start = nullptr;
    s.finish = nullptr;
    char allocatorTag = 0;
    int zero = 0;
    StringEndOfStorage(&s.eos, nullptr, &allocatorTag, &zero);
    StringInitRange(&s, nullptr, text, text + std::strlen(text), text);
}

} // namespace

int __fastcall SetFigureAnimation(void* manager, void*, void* figure, const char* modelName, const char* animation) {
    if (!animation) return ClearAnimation(figure);
    RawString key;
    MakeString(key, modelName);
    void* model = FindModelAt(manager, nullptr, &key);
    StringDeallocate(&key.eos, nullptr, key.start, static_cast<int>(key.eos - key.start));
    if (!model) {
        DebugMessageOriginal(reinterpret_cast<const char*>(0x00790DC0), modelName);
        return 0;
    }
    RawString name;
    MakeString(name, animation);
    void* found = FindAnimation(model, nullptr, &name);
    NodeDeallocate(name.start, static_cast<int>(name.eos - name.start));
    if (!found) {
        DebugMessageOriginal(reinterpret_cast<const char*>(0x00790D94), animation, modelName);
        return 0;
    }
    return SetAnimation(figure, nullptr, found, animation);
}

EI_REPLACE(0x005ACCF0, SetFigureAnimation);
