// The game's address orders settled in harness runs (harness_address_order.h; ticket T2f).
#include "harness_address_order.h"

#include "../../src/game/address_order.h"
#include "../../src/game/engine_object.h"
#include "../../src/game/game_data.h"
#include "../../src/game/net_classes.h"
#include "../../src/game/net_messages.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_briefing.h"
#include "harness_objects.h"
#include "harness_windows.h"
#include "../../src/game/terrain_sector.h"
#include "../../src/game/world.h"
#include "../../src/hook/log.h"
#include "../../src/lib/containers.h"
#include "ei/hook.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

using ObjectSet = lib::AlignedArray<std::uint32_t>;

bool g_wanted = false;
bool g_patched = false;

// The order of address_order::ObjectBefore with ById: the object's id, then its address.
bool IdBefore(std::uint32_t a, std::uint32_t b) {
    int ia = reinterpret_cast<const CEngineObject*>(a)->id_;
    int ib = reinterpret_cast<const CEngineObject*>(b)->id_;
    if (ia != ib) return ia < ib;
    return a < b;
}

// For the merge stub: whether `old` (the sent set's element) sorts before `now` (1 or 0).
int __stdcall IdBeforeForOriginal(std::uint32_t old, std::uint32_t now) { return IdBefore(old, now) ? 1 : 0; }

// 0047ADF0 (ECX the set, plain RET): sorted by id and without duplicates; the size set with the
// original's 0047B880 (RET 8), as its sort does.
void __fastcall SortForOriginal(ObjectSet* self) {
    if (self->size < 2) return;
    std::sort(self->data, self->data + self->size, IdBefore);
    std::uint32_t* end = self->data + self->size;
    std::uint32_t* out = self->data + 1;
    for (std::uint32_t* p = self->data + 1; p < end; ++p)
        if (*p != p[-1]) *out++ = *p;
    using ResizeFn = void(__fastcall*)(ObjectSet*, void*, int, int);
    ei::Code<ResizeFn>(0x0047B880)(self, nullptr, static_cast<int>(out - self->data), -1);
}

// 006C8E90 (cdecl, two slots of SetSun's sector array): by (sy, sx) of the sectors in the slots.
int __cdecl CompareSectorsForOriginal(const void* a, const void* b) {
    const auto* x = *static_cast<const terrain::SectorData* const*>(a);
    const auto* y = *static_cast<const terrain::SectorData* const*>(b);
    if (x->sy != y->sy) return x->sy < y->sy ? -1 : 1;
    if (x->sx != y->sx) return x->sx < y->sx ? -1 : 1;
    return 0;
}

// DiffObjectSets' merge 0047B4C0: CMP ECX,EAX / JZ 0047B511 / MOV [ESP+0x24],1 / JNC 0047B4F5 (ECX the
// sent element, EAX the current one; below: the sent one is gone, 0047B4CE). EAX, ECX and EDX kept.
__declspec(naked) void MergeStep() {
    __asm {
        cmp ecx, eax
        jne differ
        push 0x0047B511
        ret
    differ:
        mov dword ptr [esp + 0x24], 1
        push eax
        push ecx
        push edx
        push eax
        push ecx
        call IdBeforeForOriginal
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz added
        push 0x0047B4CE
        ret
    added:
        push 0x0047B4F5
        ret
    }
}

// 0041E430 (the client's binding handler; ECX the client, EDX the binding, plain RET): the original's
// counted assignment, then, when players came before the binding, the interface's refresh 005F82D0
// (RET 4) on game.exe's interface 007C7D50, as ours does (net_client_handlers.cpp ClientOnBinding, UB1).
void __fastcall OnBindingForOriginal(net::ClientSession* self, net::BindingMessage* binding) {
    CRefObject* previous = self->binding_;
    self->binding_ = binding;
    if (binding) ++binding->refs_;
    if (previous && --previous->refs_ == 0) delete previous;
    if (self->players_.Empty()) return;
    using RefreshFn = void(__thiscall*)(CInterface3D*, int);
    ei::Code<RefreshFn>(0x005F82D0)(&ei::OriginalGameData<CInterface3D>(0x007C7D50), 1);
}

struct Site {
    std::uintptr_t address;
    unsigned char expected[14];
    std::size_t size;   // the bytes replaced: a JMP to `target`, then NOPs
    void* target;
    const char* what;
};

const Site kSites[] = {
    {0x0047ADF0, {0x81, 0xEC, 0x08, 0x08, 0x00, 0x00}, 6, reinterpret_cast<void*>(&SortForOriginal),
     "the object sets' sort 0047ADF0"},
    {0x0047B4C0, {0x3B, 0xC8, 0x74, 0x4D, 0xC7, 0x44, 0x24, 0x24, 0x01, 0x00, 0x00, 0x00, 0x73, 0x27}, 14,
     reinterpret_cast<void*>(&MergeStep), "DiffObjectSets' merge 0047B1F0"},
    {0x006C8E90, {0x8B, 0x54, 0x24, 0x08, 0x56}, 5, reinterpret_cast<void*>(&CompareSectorsForOriginal),
     "SetSun's comparator 006C8E90"},
    // The draw lists' sorts: ours, by name with ById (SV7b6). Their insertion sort compares with the
    // first element by address inline, so the comparator 00574300 alone cannot be replaced.
    {0x005740D0, {0x83, 0xEC, 0x18, 0x53, 0x55}, 5, reinterpret_cast<void*>(&IntroSortByModel),
     "the draw lists' sort 005740D0"},
    {0x00574390, {0x83, 0xEC, 0x14, 0x8B, 0xC2}, 5, reinterpret_cast<void*>(&FinalSortByModel),
     "the draw lists' final sort 00574390"},
    // The client's player list when the update brings the players before the binding (UB1).
    {0x0041E430, {0x8B, 0x81, 0xF0, 0x25, 0x00, 0x00}, 6, reinterpret_cast<void*>(&OnBindingForOriginal),
     "the client's binding handler 0041E430"},
};

} // namespace

namespace ei::verify::address_order {

void Install() {
    ::address_order::UseIds();
    g_wanted = true;
}

void PatchOriginal() {
    if (!g_wanted || g_patched) return;
    g_patched = true;
    if (ei::Standalone()) return;
    int patched = 0;
    for (const Site& site : kSites) {
        auto* code = reinterpret_cast<unsigned char*>(site.address);
        if (std::memcmp(code, site.expected, site.size) != 0) {
            if (code[0] != 0xE9) Log("harness: address order site %08X (%s) is not as expected, left alone", unsigned(site.address), site.what);
            continue;   // a JMP: ours is hooked there
        }
        unsigned char bytes[14];
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
    Log("harness: address orders by object id, sector and name, the player list by binding (%d of %d original sites)", patched, int(sizeof(kSites) / sizeof(kSites[0])));
}

bool CheckPlayerList(std::uintptr_t briefing, std::string* text) {
    CUiWindow* top = HarnessInterface()->GetActiveWindow();
    const std::uintptr_t vtable = top ? RealWindowVtable(*reinterpret_cast<const std::uintptr_t*>(top)) : 0;
    auto* client = ei::Standalone() ? net::GameClient() : ei::OriginalGameData<net::ClientSession*>(0x0079B5D8);
    char line[200];
    bool ok = false;
    if (!top || vtable != briefing) {
        std::snprintf(line, sizeof(line), "the base screen is not on top");
    } else if (!client || !static_cast<CBriefingScreen*>(top)->chat_) {
        std::snprintf(line, sizeof(line), "no network client, or its base screen has no player list");
    } else {
        int players = 0;
        for (auto* node = client->players_.First(); node != client->players_.End(); node = node->next) ++players;
        const CRefObject* binding = client->binding_;
        const bool bound = binding && !(binding->flags7_ & CRefObject::kDetached);
        const int listed = static_cast<CChatWindow*>(static_cast<CBriefingScreen*>(top)->chat_)->listed_;
        ok = bound && players > 0 && listed == players;
        std::snprintf(line, sizeof(line), "the base screen lists %d of the client's %d players (binding %s)", listed, players,
                      bound ? "live" : "missing");
    }
    *text = line;
    Log("harness: checkplayers: %s: %s", line, ok ? "ok" : "FAIL");
    return ok;
}

} // namespace ei::verify::address_order
