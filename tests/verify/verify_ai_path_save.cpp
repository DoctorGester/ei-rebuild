// Checks for the path's save stream (src/game/ai_path_save.cpp). Saving: differential through a
// stand-in stream whose write slot records the bytes (the original, then ours), then ours into
// the real stream. Loading reads the real stream, so ours only: the list holds at least the cells
// it held before.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/core_streams.h"

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

// Recordings nest (the path's save calls the order save, whose check records too), so the buffer
// in use is put back afterwards.
void SaveInto(SaveFn fn, AIPath* path, std::vector<std::uint8_t>& out) {
    s_vtable[2] = reinterpret_cast<void*>(&RecordWrite);
    StandIn stream{s_vtable};
    std::vector<std::uint8_t>* outer = s_bytes;
    s_bytes = &out;
    fn(path, nullptr, &stream);
    s_bytes = outer;
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

// 005C8370 (A-W11): differential like SavePath (the original, then ours, into the stand-in stream),
// then ours into the real stream. Reached by saving a game with units on the move (world_load).
using SaveOrderFn = void(__fastcall*)(AIPath*, void*, CStream*);
static SaveOrderFn s_originalSaveOrder;
static void __fastcall SaveOrderInto(AIPath* path, void*, void* stream) {
    s_originalSaveOrder(path, nullptr, static_cast<CStream*>(stream));
}
static void __fastcall SaveOrderOurs(AIPath* path, void*, void* stream) { path->SaveOrder(static_cast<CStream*>(stream)); }
static void __fastcall SaveOrderVerify(AIPath* path, void*, CStream* stream) {
    std::vector<std::uint8_t> expected, actual;
    SaveInto(&SaveOrderInto, path, expected);
    SaveInto(&SaveOrderOurs, path, actual);
    ei::verify::Check("AIPath::SaveOrder", true, actual == expected, "%d bytes (original %d)",
                      static_cast<int>(actual.size()), static_cast<int>(expected.size()));
    path->SaveOrder(stream);
}
EI_VERIFY(0x005C8370, SaveOrderVerify, 6, s_originalSaveOrder);

// 005C8230 (A-W11): reads the real stream, so ours only: no step was lost. Reached by loading a
// save (world_load).
static void __fastcall LoadOrderVerify(AIPath* path, void*, CStream* stream) {
    int before = path->steps.count;
    path->LoadOrder(stream);
    ei::verify::Check("AIPath::LoadOrder", true, path->steps.count >= before, "%d steps (before %d)", path->steps.count,
                      before);
}
EI_VERIFY(0x005C8230, LoadOrderVerify, 0, s_unused);

// 005B2430.
static void __fastcall LoadPathVerify(AIPath* path, void*, void* stream) {
    int before = path->cells.count;
    LoadPath(path, nullptr, stream);
    ei::verify::Check("LoadPath", true, path->cells.count >= before, "%d cells", path->cells.count);
}
EI_VERIFY(0x005B2430, LoadPathVerify, 0, s_unused);
