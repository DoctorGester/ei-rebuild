// Script groups ordered by object id in harness runs (harness_script_order.h; ticket SV7b1).
#include "harness_script_order.h"

#include "../../src/game/script.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace {

bool g_wanted = false;
bool g_patched = false;
bool g_tieLogged = false;

} // namespace

// Whether member `x` sorts after member `y` (two different pointers) in the id order (1 or 0). The
// stubs below call it with EAX, ECX and EDX saved.
static int __stdcall IdAfterForOriginal(const CEngineObject* x, const CEngineObject* y) {
    if (x && y && x->id_ == y->id_ && !g_tieLogged) {
        g_tieLogged = true;
        ei::Log("harness: script group members %p and %p share id %d: the id order cannot settle them", x, y, x->id_);
    }
    return ScriptGroup::IdAfter(x, y) ? 1 : 0;
}

// 004B2D80, the comparator of Sort and Load (cdecl, two ScriptObjectRef*): -1 / 0 / 1 by id.
static int __cdecl CompareByIdForOriginal(const void* a, const void* b) {
    const CEngineObject* x = static_cast<const ScriptObjectRef*>(a)->object;
    const CEngineObject* y = static_cast<const ScriptObjectRef*>(b)->object;
    if (x == y) return 0;
    return IdAfterForOriginal(x, y) ? 1 : -1;
}

// The inline comparisons. Each stub does what the bytes it replaces did, then compares by id and
// jumps back: to the original's path for equal objects, for `x` above `y` (JBE not taken) or for
// `x` below `y` (JBE taken). EAX, ECX and EDX are kept across the call; the original reads no flags
// after the jump back.

// Union 004B27B2: CMP EDI,EDX / MOV [ESP+0x20],EDX / JNZ 004B27EF (there JBE 004B281B).
__declspec(naked) static void UnionStep() {
    __asm {
        mov dword ptr [esp + 0x20], edx
        cmp edi, edx
        jne differ
        push 0x004B27BA
        ret
    differ:
        push eax
        push ecx
        push edx
        push edx
        push edi
        call IdAfterForOriginal
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz below
        push 0x004B27F1
        ret
    below:
        push 0x004B281B
        ret
    }
}

// Difference 004B2A9A: MOV ECX,[ECX+EDX*4] / CMP EAX,ECX / JNZ 004B2AA9 (there JBE 004B2AB2).
__declspec(naked) static void DifferenceStep() {
    __asm {
        mov ecx, dword ptr [ecx + edx * 4]
        cmp eax, ecx
        jne differ
        push 0x004B2AA1
        ret
    differ:
        push eax
        push ecx
        push edx
        push ecx
        push eax
        call IdAfterForOriginal
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz below
        push 0x004B2AAB
        ret
    below:
        push 0x004B2AB2
        ret
    }
}

// Intersection 004B2CEE: MOV EAX,[EAX+EDX*4] / CMP ESI,EAX / JNZ 004B2D35 (there JBE 004B2D3E).
__declspec(naked) static void IntersectionStep() {
    __asm {
        mov eax, dword ptr [eax + edx * 4]
        cmp esi, eax
        jne differ
        push 0x004B2CF5
        ret
    differ:
        push eax
        push ecx
        push edx
        push eax
        push esi
        call IdAfterForOriginal
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz below
        push 0x004B2D37
        ret
    below:
        push 0x004B2D3E
        ret
    }
}

// Insert 004B2F27 (the binary search): CMP EDX,ECX / JZ 004B30FC / JBE 004B2F3A.
__declspec(naked) static void InsertStep() {
    __asm {
        cmp edx, ecx
        jne differ
        push 0x004B30FC
        ret
    differ:
        push eax
        push ecx
        push edx
        push ecx
        push edx
        call IdAfterForOriginal
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz below
        push 0x004B2F31
        ret
    below:
        push 0x004B2F3A
        ret
    }
}

namespace ei::verify::script_order {

namespace {

struct Site {
    std::uintptr_t address;
    unsigned char expected[10];
    std::size_t size;   // the bytes replaced: a JMP to `target`, then NOPs
    void* target;
    const char* what;
};

const Site kSites[] = {
    {0x004B2D80, {0x8B, 0x44, 0x24, 0x04, 0x8B}, 5, reinterpret_cast<void*>(&CompareByIdForOriginal),
     "ScriptGroup's comparator 004B2D80"},
    {0x004B27B2, {0x3B, 0xFA, 0x89, 0x54, 0x24, 0x20, 0x75, 0x35}, 8, reinterpret_cast<void*>(&UnionStep),
     "ScriptGroup::Union 004B2620"},
    {0x004B2A9A, {0x8B, 0x0C, 0x91, 0x3B, 0xC1, 0x75, 0x08}, 7, reinterpret_cast<void*>(&DifferenceStep),
     "ScriptGroup::Difference 004B2920"},
    {0x004B2CEE, {0x8B, 0x04, 0x90, 0x3B, 0xF0, 0x75, 0x40}, 7, reinterpret_cast<void*>(&IntersectionStep),
     "ScriptGroup::Intersection 004B2B60"},
    {0x004B2F27, {0x3B, 0xD1, 0x0F, 0x84, 0xCD, 0x01, 0x00, 0x00, 0x76, 0x09}, 10, reinterpret_cast<void*>(&InsertStep),
     "ScriptGroup::Insert 004B2EF0"},
};

} // namespace

void Install() {
    ScriptGroup::OrderById();
    g_wanted = true;
}

void PatchOriginal() {
    if (!g_wanted || g_patched) return;
    g_patched = true;
    if (ei::Standalone()) {
        Log("harness: script groups by object id (the rewrites only: no original code in the image)");
        return;
    }
    int patched = 0;
    for (const Site& site : kSites) {
        auto* code = reinterpret_cast<unsigned char*>(site.address);
        if (std::memcmp(code, site.expected, site.size) != 0) {
            Log("harness: script group order site %08X (%s) is not as expected, left alone", unsigned(site.address), site.what);
            continue;
        }
        unsigned char bytes[10];
        std::memset(bytes, 0x90, sizeof(bytes));
        bytes[0] = 0xE9;
        const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(site.target) - (site.address + 5));
        std::memcpy(bytes + 1, &rel, sizeof(rel));
        DWORD old;
        VirtualProtect(code, site.size, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(code, bytes, site.size);
        VirtualProtect(code, site.size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, site.size);
        ++patched;
    }
    Log("harness: script groups by object id (%d of %d original sites)", patched, int(sizeof(kSites) / sizeof(kSites[0])));
}

} // namespace ei::verify::script_order
