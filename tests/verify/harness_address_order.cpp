// The game's address orders settled in harness runs (harness_address_order.h; ticket T2f).
#include "harness_address_order.h"

#include "exe_objects.h"
#include "harness_original.h"
#include "harness_windows.h"
#include "image/image_side.h"

#include "../../src/hook/log.h"
#include "../../src/lib/intro_sort.h"
#include "ei/hook.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

namespace {

// The patched sites run in game.exe's code only: they see game.exe's objects.
using namespace ei::verify::exe;

using ObjectSet = AlignedArray<std::uint32_t>;

bool g_wanted = false;
bool g_patched = false;

// The order of address_order::ObjectBefore: the object's id, then its address.
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
    ei::verify::OriginalCode<ResizeFn>(0x0047B880)(self, nullptr, static_cast<int>(out - self->data), -1);
}

// 006C8E90 (cdecl, two slots of SetSun's sector array): by (sy, sx) of the sectors in the slots.
int __cdecl CompareSectorsForOriginal(const void* a, const void* b) {
    const auto* x = *static_cast<const SectorData* const*>(a);
    const auto* y = *static_cast<const SectorData* const*>(b);
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
void __fastcall OnBindingForOriginal(net::ClientSession* self, CRefObject* binding) {
    CRefObject* previous = self->binding_;
    self->binding_ = binding;
    if (binding) ++binding->refs_;
    if (previous && --previous->refs_ == 0) previous->Delete();
    if (self->players_.Empty()) return;
    using RefreshFn = void(__thiscall*)(void*, int);
    ei::verify::OriginalCode<RefreshFn>(0x005F82D0)(&ei::OriginalGameData<std::uint8_t>(0x007C7D50), 1);
}

// The draw lists' order by name, as ours (address_order::DrawBefore, src/game/address_order.cpp): the first part
// set's texture (null first, then by name), then the figure (null first, then by name and morph weights), then the
// view depth as the original.
const CTexture* RankTexture(const CObject3DClientSpecific* model) {
    const TextureParts* rank = model->partSets[0];
    return rank ? rank->texture : nullptr;
}

int CompareTextures(const CTexture* a, const CTexture* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    const char* aName = a->name_;
    const char* bName = b->name_;
    return std::strcmp(aName ? aName : "", bName ? bName : "");
}

int CompareFigures(const CLinkMesh* a, const CLinkMesh* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    if (a->name_ < b->name_) return -1;
    if (b->name_ < a->name_) return 1;
    for (int i = 0; i < 3; ++i) {
        const auto wa = std::bit_cast<std::uint32_t>(a->morphWeights_[i]);
        const auto wb = std::bit_cast<std::uint32_t>(b->morphWeights_[i]);
        if (wa != wb) return wa < wb ? -1 : 1;
    }
    return 0;
}

bool DrawBefore(CEngineObject* a, CEngineObject* b) {
    if (!a || !b) return false;
    const auto* objectA = static_cast<const CObject3D*>(a);
    const auto* objectB = static_cast<const CObject3D*>(b);
    const CObject3DClientSpecific* groupA = objectA->ClientSpecific();
    const CObject3DClientSpecific* groupB = objectB->ClientSpecific();
    if (!groupA || !groupB) return false;
    if (int c = CompareTextures(RankTexture(groupA), RankTexture(groupB))) return c < 0;
    if (int c = CompareFigures(objectA->figure, objectB->figure)) return c < 0;
    return objectA->worldPosition_.z < objectB->worldPosition_.z;
}

// Sorted with the game's sort, as the image sorts its draw lists (lib/intro_sort.h).
// 005740D0 (ECX first, EDX last; the value type, the depth limit and the comparator on the stack, RET 0xC).
void __fastcall IntroSortForOriginal(CEngineObject** first, CEngineObject** last, void*, int depthLimit, int) {
    lib::IntroSortLoop(first, last, depthLimit, DrawBefore);
}

// 00574390 (ECX first, EDX last, the comparator on the stack, RET 4).
void __fastcall FinalSortForOriginal(CEngineObject** first, CEngineObject** last, int) {
    lib::FinalInsertionSort(first, last, DrawBefore);
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
    // The draw lists' sorts, by name (SV7b6). Their insertion sort compares with the
    // first element by address inline, so the comparator 00574300 alone cannot be replaced.
    {0x005740D0, {0x83, 0xEC, 0x18, 0x53, 0x55}, 5, reinterpret_cast<void*>(&IntroSortForOriginal),
     "the draw lists' sort 005740D0"},
    {0x00574390, {0x83, 0xEC, 0x14, 0x8B, 0xC2}, 5, reinterpret_cast<void*>(&FinalSortForOriginal),
     "the draw lists' final sort 00574390"},
    // The client's player list when the update brings the players before the binding (UB1).
    {0x0041E430, {0x8B, 0x81, 0xF0, 0x25, 0x00, 0x00}, 6, reinterpret_cast<void*>(&OnBindingForOriginal),
     "the client's binding handler 0041E430"},
};

} // namespace

namespace ei::verify::address_order {

namespace {

// The base screen `briefing`'s player list in game.exe's run: its client (0079B5D8) and chat window.
bool OriginalPlayerList(void* briefing, image::PlayerList* list) {
    auto* client = ei::OriginalGameData<exe::net::ClientSession*>(0x0079B5D8);
    const exe::CChatWindow* chat = static_cast<const exe::CBriefingScreen*>(briefing)->chat_;
    if (!client || !chat) return false;
    list->players = 0;
    for (auto* node = client->players_.First(); node != client->players_.End(); node = node->next) ++list->players;
    const exe::CRefObject* binding = client->binding_;
    list->bound = binding && !(binding->flags7_ & exe::CRefObject::kDetached);
    list->listed = chat->listed_;
    return true;
}

} // namespace

void Install() { g_wanted = true; }

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
    void* top = TopWindow();
    const std::uintptr_t vtable = top ? WindowKind(top) : 0;
    image::PlayerList list;
    char line[200];
    bool ok = false;
    if (!top || vtable != briefing) {
        std::snprintf(line, sizeof(line), "the base screen is not on top");
    } else if (!(ei::Standalone() ? image::BriefingPlayerList(top, &list) : OriginalPlayerList(top, &list))) {
        std::snprintf(line, sizeof(line), "no network client, or its base screen has no player list");
    } else {
        ok = list.bound && list.players > 0 && list.listed == list.players;
        std::snprintf(line, sizeof(line), "the base screen lists %d of the client's %d players (binding %s)", list.listed,
                      list.players, list.bound ? "live" : "missing");
    }
    *text = line;
    Log("harness: checkplayers: %s: %s", line, ok ? "ok" : "FAIL");
    return ok;
}

} // namespace ei::verify::address_order
