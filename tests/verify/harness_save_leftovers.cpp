// Save-only leftovers made canonical in the original (harness_save_leftovers.h; ticket SV7b2a).
#include "harness_save_leftovers.h"

#include "../../src/game/chunk_archive.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <iterator>

namespace ei::verify::save_leftovers {

namespace {

// ---- Inline stores ----------------------------------------------------------------------------------

// `original` (checked first) becomes `code`: in place (NOP-filled) when it fits, else a JMP to a stub
// that runs `code` and jumps back after the replaced bytes. `code` has no relative operand.
struct InlineSite {
    std::uintptr_t at;
    std::uint8_t originalSize;
    std::uint8_t original[8];
    std::uint8_t codeSize;
    std::uint8_t code[24];
    const char* what;
};

// MOV byte ptr [ESP+d],0 (a step's flags) -> MOV word ptr [ESP+d],0 (flags and padding).
#define EI_STEP_FLAGS(at, d, what) {at, 5, {0xC6, 0x44, 0x24, d, 0x00}, 7, {0x66, 0xC7, 0x44, 0x24, d, 0x00, 0x00}, what}

const InlineSite kInlineSites[] = {
    EI_STEP_FLAGS(0x00544D01, 0x1A, "a step's padding zeroed (AddStep), SV7b2a"),
    EI_STEP_FLAGS(0x00544D83, 0x1A, "a step's padding zeroed (AddEmote), SV7b2a"),
    EI_STEP_FLAGS(0x005504DB, 0x22, "a step's padding zeroed (BuildOrderSteps), SV7b2a"),
    EI_STEP_FLAGS(0x00550D0F, 0x1A, "a step's padding zeroed (BuildDeathAnimation), SV7b2a"),
    EI_STEP_FLAGS(0x0055102D, 0x1A, "a step's padding zeroed (IdleAnimation), SV7b2a"),
    EI_STEP_FLAGS(0x00551123, 0x1A, "a step's padding zeroed (IdleAnimation), SV7b2a"),
    EI_STEP_FLAGS(0x00551343, 0x1A, "a step's padding zeroed (BuildMoveSteps), SV7b2a"),
    EI_STEP_FLAGS(0x00551399, 0x1A, "a step's padding zeroed (BuildMoveSteps), SV7b2a"),
    EI_STEP_FLAGS(0x00551869, 0x1A, "a step's padding zeroed (BuildTransition), SV7b2a"),
    // BuildIdle: MOV [ESP+0x20],EBX; MOV [ESP+0x1A],BL, and MOV ECX,ESI; MOV [ESP+0x1A],BL; then the
    // padding byte [ESP+0x1B] = 0.
    {0x00550DEA, 8, {0x89, 0x5C, 0x24, 0x20, 0x88, 0x5C, 0x24, 0x1A}, 13,
     {0x89, 0x5C, 0x24, 0x20, 0x88, 0x5C, 0x24, 0x1A, 0xC6, 0x44, 0x24, 0x1B, 0x00},
     "a step's padding zeroed (BuildIdle), SV7b2a"},
    {0x00550ED0, 6, {0x8B, 0xCE, 0x88, 0x5C, 0x24, 0x1A}, 11,
     {0x8B, 0xCE, 0x88, 0x5C, 0x24, 0x1A, 0xC6, 0x44, 0x24, 0x1B, 0x00}, "a step's padding zeroed (BuildIdle), SV7b2a"},
    // The step constructor 00550A60: MOV byte [EAX+1],0xFF; MOV byte [EAX],0xFF -> MOV dword [EAX],0xFFFF
    // (its MOV [EAX+2],CL with CL 0 follows).
    {0x00550A64, 7, {0xC6, 0x40, 0x01, 0xFF, 0xC6, 0x00, 0xFF}, 7, {0xC7, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x90},
     "a blank step's padding zeroed, SV7b2a"},
    // AddRect: MOV byte [ESP+0x10],1 -> MOV dword [ESP+0x10],1 (isRect and its padding).
    {0x004B1EFF, 5, {0xC6, 0x44, 0x24, 0x10, 0x01}, 8, {0xC7, 0x44, 0x24, 0x10, 0x01, 0x00, 0x00, 0x00},
     "a script area part's padding zeroed (AddRect), SV7b2a"},
    // AddRound: MOV byte [ESP+0x10],0 -> MOV dword [ESP+0x10],0, and y2 ([ESP+0x20], never set) 0.
    {0x004B209F, 5, {0xC6, 0x44, 0x24, 0x10, 0x00}, 16,
     {0xC7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00},
     "a round script area part's padding and y2 zeroed (AddRound), SV7b2a"},
    // CEffectSound::Reset: MOV byte [ESI+0x14C],BL (BL 0) -> the whole name_ zeroed (EDI, ECX, EAX kept).
    {0x006762C4, 6, {0x88, 0x9E, 0x4C, 0x01, 0x00, 0x00}, 21,
     {0x57, 0x51, 0x50, 0x8D, 0xBE, 0x4C, 0x01, 0x00, 0x00, 0xB9, 0x40, 0x00, 0x00, 0x00, 0x33, 0xC0, 0xF3, 0xAB,
      0x58, 0x59, 0x5F},
     "a sound effect's whole name zeroed (Reset), SV7b2a"},
    // ItemStock::SaveLists: MOV byte [ESP+0x1D],0 -> MOV word [ESP+0x1C],0 (the code copy's fifth byte too).
    {0x0069BCD3, 5, {0xC6, 0x44, 0x24, 0x1D, 0x00}, 7, {0x66, 0xC7, 0x44, 0x24, 0x1C, 0x00, 0x00},
     "a stock modifier code's fifth byte zeroed (SaveLists), SV7b2a"},
    // CallsAddClaim 005CCEA0 never sets its stack record's point: the three loads MOV EDX,[ESP+d] of the
    // copy become XOR EDX,EDX (a claim's point 0,0,0, as ours writes it; `memory.callN.point`), S20d.
    {0x005CCF04, 4, {0x8B, 0x54, 0x24, 0x08}, 2, {0x33, 0xD2}, "a heard claim's point.x zeroed (CallsAddClaim), S20d"},
    {0x005CCF0C, 4, {0x8B, 0x54, 0x24, 0x0C}, 2, {0x33, 0xD2}, "a heard claim's point.y zeroed (CallsAddClaim), S20d"},
    {0x005CCF13, 4, {0x8B, 0x54, 0x24, 0x10}, 2, {0x33, 0xD2}, "a heard claim's point.z zeroed (CallsAddClaim), S20d"},
};

#undef EI_STEP_FLAGS

void Write(std::uintptr_t at, const void* bytes, std::size_t size) {
    DWORD old;
    VirtualProtect(reinterpret_cast<void*>(at), size, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(reinterpret_cast<void*>(at), bytes, size);
    VirtualProtect(reinterpret_cast<void*>(at), size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), size);
}

void PutJump(std::uint8_t* at, std::uintptr_t from, std::uintptr_t to) {
    at[0] = 0xE9;
    std::int32_t rel = static_cast<std::int32_t>(to - (from + 5));
    std::memcpy(at + 1, &rel, 4);
}

// Returns the stub bytes used (0 when the site was left alone).
std::size_t PatchInline(const InlineSite& site, std::uint8_t* stub) {
    if (std::memcmp(reinterpret_cast<const void*>(site.at), site.original, site.originalSize) != 0) {
        Log("harness: %08X does not hold the expected store; left as it is", unsigned(site.at));
        return 0;
    }
    std::uint8_t bytes[8];
    std::memset(bytes, 0x90, sizeof(bytes));
    std::size_t used = 0;
    if (site.codeSize <= site.originalSize) {
        std::memcpy(bytes, site.code, site.codeSize);
    } else {
        std::memcpy(stub, site.code, site.codeSize);
        std::uintptr_t back = reinterpret_cast<std::uintptr_t>(stub) + site.codeSize;
        PutJump(stub + site.codeSize, back, site.at + site.originalSize);
        FlushInstructionCache(GetCurrentProcess(), stub, site.codeSize + 5u);
        PutJump(bytes, site.at, reinterpret_cast<std::uintptr_t>(stub));
        used = site.codeSize + 5u;
    }
    ei::NoteHarnessPatch(site.at, site.originalSize, site.what);
    Write(site.at, bytes, site.originalSize);
    return used ? used : 1;
}

// ---- The chunk archive's keys -----------------------------------------------------------------------

constexpr std::uintptr_t kAppendChild = 0x00409110;
constexpr std::uintptr_t kWriteStreamChunk = 0x0040A3C0;
using AppendChildFn = bool(__fastcall*)(ChunkArchive*, void*, ChunkArchive::Ref*, int, const void*, std::uint32_t);

// Per archive being saved, its objects in order of first save (number = index + 1).
constexpr int kArchives = 4;
constexpr int kObjects = 4096;
struct Numbering {
    const ChunkArchive* archive;
    int count;
    std::uint32_t objects[kObjects];
};
Numbering g_numberings[kArchives];
bool g_full = false;

std::uint32_t Number(const ChunkArchive* archive, std::uint32_t object) {
    Numbering* numbering = nullptr;
    for (Numbering& n : g_numberings)
        if (n.archive == archive) numbering = &n;
    for (Numbering& n : g_numberings)
        if (!numbering && !n.archive) {
            numbering = &n;
            n.archive = archive;
            n.count = 0;
        }
    if (numbering) {
        for (int i = 0; i < numbering->count; ++i)
            if (numbering->objects[i] == object) return static_cast<std::uint32_t>(i + 1);
        if (numbering->count < kObjects) {
            numbering->objects[numbering->count++] = object;
            return static_cast<std::uint32_t>(numbering->count);
        }
    }
    if (!g_full) Log("harness: archive %p: no room to number its objects; the address is written", archive);
    g_full = true;
    return object;
}

// SaveObjectRef's field and Close's record key (0040A589, 0040A205): the object's number.
bool __fastcall NumberedAppendChild(ChunkArchive* archive, void*, ChunkArchive::Ref* chunk, int id, const void* data,
                                    std::uint32_t size) {
    std::uint32_t object;
    std::memcpy(&object, data, 4);
    std::uint32_t number = Number(archive, object);
    return ei::Code<AppendChildFn>(kAppendChild)(archive, nullptr, chunk, id, &number, size);
}

// Close's index (0040A2D3): per object its class id and its address, the address now its number; the
// archive's numbering ends with it. The index stream is the archive's first member.
bool __fastcall NumberedIndexWrite(CStream* out, int id, CMemoryStream* index) {
    const auto* archive = reinterpret_cast<const ChunkArchive*>(index);
    for (std::uint32_t at = 4; at + 4 <= index->size_; at += 8) {
        std::uint32_t object;
        std::memcpy(&object, index->data_ + at, 4);
        std::uint32_t number = Number(archive, object);
        std::memcpy(index->data_ + at, &number, 4);
    }
    for (Numbering& n : g_numberings)
        if (n.archive == archive) n.archive = nullptr;
    return reinterpret_cast<decltype(&WriteStreamChunk)>(kWriteStreamChunk)(out, id, index);
}

struct CallSite {
    std::uintptr_t call;     // a CALL rel32
    std::uintptr_t target;   // what it calls
    const void* numbered;
};
const CallSite kCallSites[] = {
    {0x0040A589, kAppendChild, reinterpret_cast<const void*>(&NumberedAppendChild)},   // SaveObjectRef
    {0x0040A205, kAppendChild, reinterpret_cast<const void*>(&NumberedAppendChild)},   // Close: a record's key
    {0x0040A2D3, kWriteStreamChunk, reinterpret_cast<const void*>(&NumberedIndexWrite)},   // Close: the index
};

bool Redirect(const CallSite& site) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(site.call);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8 || site.call + 5 + static_cast<std::uintptr_t>(rel) != site.target) {
        Log("harness: %08X is not a call of %08X; the archive keeps addresses there", unsigned(site.call),
            unsigned(site.target));
        return false;
    }
    std::uint8_t bytes[5] = {0xE8};
    rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(site.numbered) - (site.call + 5));
    std::memcpy(bytes + 1, &rel, 4);
    ei::NoteHarnessPatch(site.call, 5, "a chunk archive key numbered by first save, SV7b2a");
    Write(site.call, bytes, sizeof(bytes));
    return true;
}

} // namespace

void Install() {
    if (ei::Standalone()) return;   // ours only
    char hooks[8] = "";
    GetEnvironmentVariableA("EI_NO_HOOKS", hooks, sizeof(hooks));
    if (hooks[0] != '1') return;   // hooked: ours writes the canonical values
    char value[8] = "";
    if (GetEnvironmentVariableA("EI_SAVE_LEFTOVERS", value, sizeof(value)) && value[0] == '0') {
        Log("harness: save leftovers left as the original writes them (EI_SAVE_LEFTOVERS=0)");
        return;
    }
    auto* stubs = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stubs) {
        Log("harness: no page for the save leftovers' stubs; the original left as it is");
        return;
    }
    int patched = 0;
    std::size_t used = 0;
    for (const InlineSite& site : kInlineSites) {
        std::size_t n = PatchInline(site, stubs + used);
        if (n) ++patched;
        if (n > 1) used += (n + 15) & ~std::size_t(15);
    }
    for (const CallSite& site : kCallSites)
        if (Redirect(site)) ++patched;
    Log("harness: save leftovers canonical in the original (%d of %d sites)", patched,
        static_cast<int>(std::size(kInlineSites) + std::size(kCallSites)));
}

} // namespace ei::verify::save_leftovers
