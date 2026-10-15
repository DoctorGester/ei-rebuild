// STLport sort instantiations over game elements, at their library addresses (stl_sorts.h).
#include "stl_sorts.h"

#include "net_messages.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp_algo.h"

#include <cstdint>

namespace stl_sorts {
namespace {

struct ByChunkIndex {
    bool operator()(const net::FileChunkMessage* a, const net::FileChunkMessage* b) const { return a->index_ < b->index_; }
};
using ChunkSort = stlp::Sort<net::FileChunkMessage*, ByChunkIndex>;

struct ByObjectAddress {
    bool operator()(const net::Replicated& a, const net::Replicated& b) const {
        return reinterpret_cast<std::uintptr_t>(a.object) < reinterpret_cast<std::uintptr_t>(b.object);
    }
};
using ReplicatedSort = stlp::Sort<net::Replicated, ByObjectAddress>;

struct ByDepth {
    DepthLess less;
    bool operator()(const ScreenEntry& a, const ScreenEntry& b) const {
        DepthLess copy = less;
        return copy.Less(reinterpret_cast<const int*>(&a), reinterpret_cast<const int*>(&b));
    }
};
using ScreenSort = stlp::Sort<ScreenEntry, ByDepth>;

ByDepth DepthOrder(const float* plane, void* manager) { return ByDepth{{const_cast<float*>(plane), manager}}; }

struct IntLess {
    bool operator()(int a, int b) const { return a < b; }
};
using IdSort = stlp::Sort<int, IntLess>;

int CompareNames(const mfc::CString& a, const mfc::CString& b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(a)),
                           reinterpret_cast<const unsigned char*>(static_cast<const char*>(b)));
}

struct ByColumn {
    int key;
    bool operator()(const MpAddressEntry& a, const MpAddressEntry& b) const {
        switch (key) {
        case 1: return CompareNames(a.name, b.name) < 0;
        case 2: return CompareNames(a.name, b.name) > 0;
        case 3: return a.players < b.players;
        case 4: return b.players < a.players;
        case 5: return a.gameType < b.gameType;
        case 6: return b.gameType < a.gameType;
        case 7: return a.ping < b.ping;
        case 8: return b.ping < a.ping;
        default: return a.state < b.state;
        }
    }
};
using ServerSort = stlp::Sort<MpAddressEntry, ByColumn>;

} // namespace

void __fastcall ChunkIntroSort(net::FileChunkMessage** first, net::FileChunkMessage** last, void*, int depth, int) {
    ChunkSort::IntroLoop(first, last, depth, ByChunkIndex());
}
void __fastcall ChunkFinalSort(net::FileChunkMessage** first, net::FileChunkMessage** last, int) {
    ChunkSort::FinalInsertionSort(first, last, ByChunkIndex());
}

void __fastcall ReplicatedIntroSort(net::Replicated* first, net::Replicated* last, void*, int depth, void*) {
    ReplicatedSort::IntroLoop(first, last, depth, ByObjectAddress());
}
void __fastcall ReplicatedFinalSort(net::Replicated* first, net::Replicated* last, void*) {
    ReplicatedSort::FinalInsertionSort(first, last, ByObjectAddress());
}

void __fastcall ScreenIntroSort(ScreenEntry* first, ScreenEntry* last, void*, int depth, const float* plane, void* manager) {
    ScreenSort::IntroLoop(first, last, depth, DepthOrder(plane, manager));
}
void __fastcall ScreenInsertionSort(ScreenEntry* first, ScreenEntry* last, const float* plane, void* manager) {
    ScreenSort::InsertionSort(first, last, DepthOrder(plane, manager));
}
void __fastcall ScreenUnguardedInsertionSort(ScreenEntry* first, ScreenEntry* last, void*, const float* plane,
                                             void* manager) {
    ScreenSort::UnguardedInsertionSort(first, last, DepthOrder(plane, manager));
}

void __fastcall IdIntroSort(int* first, int* last, const int*, int depth) { IdSort::IntroLoop(first, last, depth, IntLess()); }
void __fastcall IdFinalSort(int* first, int* last) { IdSort::FinalInsertionSort(first, last, IntLess()); }

void __fastcall ServerIntroSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry*, int depth, int key) {
    ServerSort::IntroLoop(first, last, depth, ByColumn{key});
}
void __fastcall ServerInsertionSort(MpAddressEntry* first, MpAddressEntry* last, int key) {
    ServerSort::InsertionSort(first, last, ByColumn{key});
}
void __fastcall ServerUnguardedInsertionSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry*, int key) {
    ServerSort::UnguardedInsertionSort(first, last, ByColumn{key});
}

EI_REPLACE(0x0042A020, ChunkIntroSort);
EI_REPLACE(0x0042A150, ChunkFinalSort);
EI_REPLACE(0x0043F870, ReplicatedIntroSort);
EI_REPLACE(0x0043F9B0, ReplicatedFinalSort);
EI_REPLACE(0x00573740, ScreenIntroSort);
EI_REPLACE(0x00575BC0, ScreenInsertionSort);
EI_REPLACE(0x00576D40, ScreenUnguardedInsertionSort);
EI_REPLACE(0x0060B1E0, IdIntroSort);
EI_REPLACE(0x0064B750, IdFinalSort);
EI_REPLACE(0x0061EF60, ServerIntroSort);
EI_REPLACE(0x0061F6F0, ServerInsertionSort);
EI_REPLACE(0x0061FB00, ServerUnguardedInsertionSort);

} // namespace stl_sorts
