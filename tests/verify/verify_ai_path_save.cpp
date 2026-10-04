// Checks for the path's save stream (src/game/ai_path_save.cpp). Saving: differential through a
// stand-in stream whose write slot records the bytes (the original, then ours), then ours into
// the real stream. Loading reads the real stream, so ours only: the list holds at least the cells
// it held before.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>
#include <vector>

void __fastcall LoadPath(AIPath* path, void*, void* stream);
void __fastcall SavePath(AIPath* path, void*, void* stream);

namespace {
using SaveFn = void(__fastcall*)(AIPath*, void*, void*);
SaveFn s_originalSave;
void* s_unused;

std::vector<std::uint8_t>* s_bytes;
void __fastcall RecordWrite(void*, void*, const void* buffer, int size) {
    auto* p = static_cast<const std::uint8_t*>(buffer);
    s_bytes->insert(s_bytes->end(), p, p + size);
}
void* s_vtable[8];
struct StandIn {
    void** vtable;
};

void SaveInto(SaveFn fn, AIPath* path, std::vector<std::uint8_t>& out) {
    s_vtable[2] = reinterpret_cast<void*>(&RecordWrite);
    StandIn stream{s_vtable};
    s_bytes = &out;
    fn(path, nullptr, &stream);
}
} // namespace

// 005B2570.
static void __fastcall SavePathVerify(AIPath* path, void*, void* stream) {
    std::vector<std::uint8_t> expected, actual;
    SaveInto(s_originalSave, path, expected);
    SaveInto(&SavePath, path, actual);
    ei::verify::Check("SavePath", true, actual == expected, "%d bytes (original %d)", static_cast<int>(actual.size()),
                      static_cast<int>(expected.size()));
    SavePath(path, nullptr, stream);
}
EI_VERIFY(0x005B2570, SavePathVerify, 8, s_originalSave);

// 005B2430.
static void __fastcall LoadPathVerify(AIPath* path, void*, void* stream) {
    int before = path->cells.count;
    LoadPath(path, nullptr, stream);
    ei::verify::Check("LoadPath", true, path->cells.count >= before, "%d cells", path->cells.count);
}
EI_VERIFY(0x005B2430, LoadPathVerify, 0, s_unused);
