// STLport sort instantiations over game elements, at their library addresses (stl_sorts.h).
#include "stl_sorts.h"

#include "net_messages.h"
#include "quest_records.h"
#include "script.h"
#include "ui.h"

#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_algo.h"

#include <cstdint>
#include <string>

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
        return copy.Less(&a, &b);
    }
};
using ScreenSort = stlp::Sort<ScreenEntry, ByDepth>;

ByDepth DepthOrder(const float* plane, CMapObjectManager* manager) { return ByDepth{{const_cast<float*>(plane), manager}}; }

struct IntLess {
    bool operator()(int a, int b) const { return a < b; }
};
using IdSort = stlp::Sort<int, IntLess>;

int CompareNames(const std::string& a, const std::string& b) {
    return gamecrt::mbscmp(a,
                           b);
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

struct ByQuestOrder {
    bool operator()(const mfc::CString& a, const mfc::CString& b) const {
        const QuestRecord* first = TheQuestDatabase().FindQuest(a);
        const QuestRecord* second = TheQuestDatabase().FindQuest(b);
        return !first || (second && static_cast<unsigned>(first->order) > static_cast<unsigned>(second->order));
    }
};
using QuestSort = stlp::Sort<mfc::CString, ByQuestOrder>;

struct SaveEntryOps {
    static void Construct(SaveEntry* at, const SaveEntry& from) { at->CopyConstruct(&from); }
    static void Assign(SaveEntry& to, const SaveEntry& from) { to.Assign(&from); }
    static void Destroy(SaveEntry* p) { p->Destruct(); }
};
// Every helper tests a.savedAt < b.savedAt and takes the other branch: the order is
// "not older than".
struct NotOlder {
    bool operator()(const SaveEntry& a, const SaveEntry& b) const { return !(a.savedAt_ < b.savedAt_); }
};
using SaveSort = stlp::Sort<SaveEntry, NotOlder, SaveEntryOps>;

struct ByTextIgnoringCase {
    bool operator()(const std::string& a, const std::string& b) const { return libcrt::Stricmp(a.data(), b.data()) < 0; }
};
using TopicSort = stlp::Sort<std::string, ByTextIgnoringCase, stlp::ObjectElement<std::string>>;

struct ByName {
    bool operator()(const mfc::CString& a, const mfc::CString& b) const { return CompareNames(a.GetString(), b.GetString()) < 0; }
};
using NameSort = stlp::Sort<mfc::CString, ByName>;

} // namespace

void ChunkIntroSort(net::FileChunkMessage** first, net::FileChunkMessage** last, void*, int depth, int) {
    ChunkSort::IntroLoop(first, last, depth, ByChunkIndex());
}
void ChunkFinalSort(net::FileChunkMessage** first, net::FileChunkMessage** last, int) {
    ChunkSort::FinalInsertionSort(first, last, ByChunkIndex());
}

void ReplicatedIntroSort(net::Replicated* first, net::Replicated* last, void*, int depth, void*) {
    ReplicatedSort::IntroLoop(first, last, depth, ByObjectAddress());
}
void ReplicatedFinalSort(net::Replicated* first, net::Replicated* last, void*) {
    ReplicatedSort::FinalInsertionSort(first, last, ByObjectAddress());
}

void ScreenIntroSort(ScreenEntry* first, ScreenEntry* last, void*, int depth, const float* plane, CMapObjectManager* manager) {
    ScreenSort::IntroLoop(first, last, depth, DepthOrder(plane, manager));
}
void ScreenInsertionSort(ScreenEntry* first, ScreenEntry* last, const float* plane, CMapObjectManager* manager) {
    ScreenSort::InsertionSort(first, last, DepthOrder(plane, manager));
}
void ScreenUnguardedInsertionSort(ScreenEntry* first, ScreenEntry* last, void*, const float* plane,
                                             CMapObjectManager* manager) {
    ScreenSort::UnguardedInsertionSort(first, last, DepthOrder(plane, manager));
}

void IdIntroSort(int* first, int* last, const int*, int depth) { IdSort::IntroLoop(first, last, depth, IntLess()); }
void IdFinalSort(int* first, int* last) { IdSort::FinalInsertionSort(first, last, IntLess()); }

void ServerIntroSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry*, int depth, int key) {
    ServerSort::IntroLoop(first, last, depth, ByColumn{key});
}
void ServerInsertionSort(MpAddressEntry* first, MpAddressEntry* last, int key) {
    ServerSort::InsertionSort(first, last, ByColumn{key});
}
void ServerUnguardedInsertionSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry*, int key) {
    ServerSort::UnguardedInsertionSort(first, last, ByColumn{key});
}

void QuestIntroSort(mfc::CString* first, mfc::CString* last, void*, int depth, int) {
    QuestSort::IntroLoop(first, last, depth, ByQuestOrder());
}
void QuestInsertionSort(mfc::CString* first, mfc::CString* last, int) {
    QuestSort::InsertionSort(first, last, ByQuestOrder());
}
void QuestUnguardedInsertionSort(mfc::CString* first, mfc::CString* last, void*, int) {
    QuestSort::UnguardedInsertionSort(first, last, ByQuestOrder());
}

void SaveIntroSort(SaveEntry* first, SaveEntry* last, void*, int depth, int) {
    SaveSort::IntroLoop(first, last, depth, NotOlder());
}
void SaveFinalSort(SaveEntry* first, SaveEntry* last, int) { SaveSort::FinalInsertionSort(first, last, NotOlder()); }
SaveEntry* SaveCopyBackward(SaveEntry* first, SaveEntry* last, SaveEntry* result) {
    return stlp::CopyBackward<SaveEntry, SaveEntryOps>(first, last, result);
}

void TopicIntroSort(std::string* first, std::string* last, int, int depth, int) {
    TopicSort::IntroLoop(first, last, depth, ByTextIgnoringCase());
}
void TopicFinalSort(std::string* first, std::string* last, int) {
    TopicSort::FinalInsertionSort(first, last, ByTextIgnoringCase());
}

void NameIntroSort(mfc::CString* first, mfc::CString* last, mfc::CString*, int depth) {
    NameSort::IntroLoop(first, last, depth, ByName());
}
void NameFinalSort(mfc::CString* first, mfc::CString* last) { NameSort::FinalInsertionSort(first, last, ByName()); }

} // namespace stl_sorts
