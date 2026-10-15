// Checks for the STLport sort instantiations over game elements (src/game/stl_sorts.cpp), all
// against the original: it sorts a copy of the range, ours the range itself, and the two orders
// are compared element by element (sort is unstable: equal keys must land in the same order). The
// original's recursive calls land on these wrappers again, so every recursion level is checked.
// Nothing here allocates on the game's heap (the copies live in the DLL's). The suite sorts short
// ranges only (the hits and pairs stay under 17), so a start-up check sorts random arrays with many
// equal keys through the originals and ours, as the callers do (introsort loop, then the final
// insertion sort), including depth limits that reach the heap sort.
//
// The two sorts that allocate (S12d) run ours first on the range itself (docs/notes/
// standalone-verification.md, SV2f), then the original on a copy:
// - The objectives' quests: every comparison looks names up in the quest table (a new name is
//   added), so ours makes the additions in the original's order; the copy (CString copies share
//   the strings) then only finds them, its keys and pairs going back to the pool in reverse.
//   Ranges with a name that has no record are not ordered (the original's unguarded scans run off
//   them): logged, ours alone. No start-up check (the comparison reads the game's quest table).
// - The save list: SaveEntry copies allocate (CStringArray::Copy), so the original sorts a copy that
//   holds only each saved time and its position (empty strings and arrays, which copy without
//   allocating), and ours's order is compared through each entry's strings, time, flag and array
//   contents. Its comparison is "not older" (00658020 is a less, every caller takes its other
//   branch), so equal times compare true both ways: where the unguarded scans could leave the range
//   (over 16 entries) and two saves have the same time, ours sorts alone (logged). copy_backward is
//   checked ours only (the entries written equal the ones read). A
//   start-up check sorts random key-only entries and, with small arrays, compares each position's
//   array size and capacity too (capacities follow the whole copy and assignment sequence).
//
// S12f, ours first as well:
// - A network client's briefing topics (stlp::string by _stricmp): copies allocate, so the texts are
//   kept in our memory, ours sorts the range, then the original sorts a twin of game strings made
//   from the kept texts (freed last to first) and the two are compared text by text (texts equal
//   but for case show the order of equals). Not reached in the suite (only a network client sorts
//   them): a start-up check sorts random topics both ways, comparing each position's text and
//   capacity (assignments keep a larger block, so capacities follow the moves).
// - The hero screen's figure names (CString by _mbscmp): copies only share the strings; the copy
//   the original sorts is made before ours runs (references only) and compared by data pointer. A
//   start-up check sorts random names both ways.
#include "verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/game/map_object.h"
#include "../../src/game/net_messages.h"
#include "../../src/game/quest_records.h"
#include "../../src/game/script.h"
#include "../../src/game/ui.h"
#include "../../src/game/stl_sorts.h"
#include "../../src/hook/log.h"
#include "../../src/lib/stlp_string.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace stl_sorts;

namespace {

// A range with first > last is what an original's recursion passes after a partition ran past the
// end (a comparison that is not a strict weak ordering); the original returns at once there.
template <typename T, typename Original, typename Ours, typename Same>
void CompareSort(const char* name, T* first, T* last, Original original, Ours ours, Same same) {
    if (last < first) {
        original(first, last);
        return;
    }
    std::vector<T> scratch(first, last);
    T* s = scratch.data();
    original(s, s + scratch.size());
    ours(first, last);
    int mismatch = -1;
    for (std::size_t i = 0; i < scratch.size(); ++i)
        if (!same(scratch[i], first[i])) {
            mismatch = static_cast<int>(i);
            break;
        }
    ei::verify::Check(name, -1, mismatch, "%d elements, first difference at %d", static_cast<int>(scratch.size()), mismatch);
}

// __unguarded_insertion_sort reads before the range until an element is not greater. Its callers
// pass the range after the first 16 elements of the sort's own range (sorted by then), so the copy
// the original sorts starts 16 before `first`.
constexpr int kGuard = 16;

template <typename T, typename Original, typename Ours, typename Same>
void CompareUnguarded(const char* name, T* first, T* last, Original original, Ours ours, Same same) {
    std::vector<T> scratch(first - kGuard, last);
    original(scratch.data() + kGuard, scratch.data() + scratch.size());
    ours(first, last);
    int mismatch = -1;
    for (int i = 0; i < static_cast<int>(scratch.size()); ++i)
        if (!same(scratch[i], first[i - kGuard])) {
            mismatch = i - kGuard;
            break;
        }
    ei::verify::Check(name, -1, mismatch, "%d elements, first difference at %d", static_cast<int>(last - first), mismatch);
}

bool SameBytes(const void* a, const void* b, std::size_t n) { return std::memcmp(a, b, n) == 0; }

// ---- File chunks ---------------------------------------------------------------------------------

using Chunk = net::FileChunkMessage*;
bool SameChunk(Chunk a, Chunk b) { return a == b; }

// ---- Replicated pairs ------------------------------------------------------------------------------

bool SameReplicated(const net::Replicated& a, const net::Replicated& b) {
    return a.object == b.object && a.priority == b.priority;
}

// ---- Screen hits ---------------------------------------------------------------------------------

bool SameScreen(const ScreenEntry& a, const ScreenEntry& b) { return SameBytes(&a, &b, sizeof(ScreenEntry)); }

bool NotFinite(float x) {
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u;
}

// DepthLess is a strict weak ordering only while every object exists and every depth is a number:
// with a NaN depth Less(x, x) is true and the unguarded scans run past the range, reading what
// lies beyond it. Such ranges are logged and left to the original alone, on the real array (a
// scratch copy would read different memory past its ends).
bool Orderable(const char* name, ScreenEntry* first, ScreenEntry* last, const float* plane, void* manager) {
    if (last < first) return true;
    bool ok = true;
    for (int i = 8; i < 12; ++i) ok = ok && !NotFinite(plane[i]);
    for (ScreenEntry* p = first; ok && p != last; ++p) {
        CEngineObject* object = static_cast<CBaseManager*>(manager)->FindObject(p->objectId);
        if (!object) {
            ok = false;
            break;
        }
        const CMapObject* map = static_cast<const CMapObject*>(object);
        ok = !NotFinite(map->position.x) && !NotFinite(map->position.y) && !NotFinite(map->position.z);
    }
    if (!ok) {
        static int s_logged = 0;
        if (s_logged < 10) {
            ++s_logged;
            ei::Log("%s: %d hits not orderable (a missing object or a non-finite depth): original only", name,
                    static_cast<int>(last - first));
        }
    }
    return ok;
}

// ---- Server list --------------------------------------------------------------------------------

bool SameServer(const MpAddressEntry& a, const MpAddressEntry& b) {
    return static_cast<const char*>(a.name) == static_cast<const char*>(b.name) &&
           static_cast<const char*>(a.description) == static_cast<const char*>(b.description) && a.players == b.players &&
           a.maxPlayers == b.maxPlayers && a.ping == b.ping && SameBytes(&a.address, &b.address, sizeof(a.address)) &&
           a.gameType == b.gameType && a.foreign == b.foreign && a.state == b.state && a.password == b.password;
}

} // namespace

// 0042A020: PUSH EBX; PUSH EBP; PUSH ESI; MOV EBX,EDX (5 bytes).
static void(__fastcall* s_chunkIntro)(Chunk*, Chunk*, void*, int, int);
static void __fastcall ChunkIntroVerify(Chunk* first, Chunk* last, void* tag, int depth, int comp) {
    CompareSort(
        "ChunkIntroSort", first, last, [&](Chunk* f, Chunk* l) { s_chunkIntro(f, l, tag, depth, comp); },
        [&](Chunk* f, Chunk* l) { ChunkIntroSort(f, l, tag, depth, comp); }, SameChunk);
}
EI_VERIFY(0x0042A020, ChunkIntroVerify, 5, s_chunkIntro);

// 0042A150: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,EDX (6 bytes).
static void(__fastcall* s_chunkFinal)(Chunk*, Chunk*, int);
static void __fastcall ChunkFinalVerify(Chunk* first, Chunk* last, int comp) {
    CompareSort(
        "ChunkFinalSort", first, last, [&](Chunk* f, Chunk* l) { s_chunkFinal(f, l, comp); },
        [&](Chunk* f, Chunk* l) { ChunkFinalSort(f, l, comp); }, SameChunk);
}
EI_VERIFY(0x0042A150, ChunkFinalVerify, 6, s_chunkFinal);

// 0043F870: SUB ESP,0x10; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_replicatedIntro)(net::Replicated*, net::Replicated*, void*, int, void*);
static void __fastcall ReplicatedIntroVerify(net::Replicated* first, net::Replicated* last, void* tag, int depth, void* comp) {
    CompareSort(
        "ReplicatedIntroSort", first, last,
        [&](net::Replicated* f, net::Replicated* l) { s_replicatedIntro(f, l, tag, depth, comp); },
        [&](net::Replicated* f, net::Replicated* l) { ReplicatedIntroSort(f, l, tag, depth, comp); }, SameReplicated);
}
EI_VERIFY(0x0043F870, ReplicatedIntroVerify, 5, s_replicatedIntro);

// 0043F9B0: SUB ESP,0xC; MOV EAX,EDX (5 bytes).
static void(__fastcall* s_replicatedFinal)(net::Replicated*, net::Replicated*, void*);
static void __fastcall ReplicatedFinalVerify(net::Replicated* first, net::Replicated* last, void* comp) {
    CompareSort(
        "ReplicatedFinalSort", first, last, [&](net::Replicated* f, net::Replicated* l) { s_replicatedFinal(f, l, comp); },
        [&](net::Replicated* f, net::Replicated* l) { ReplicatedFinalSort(f, l, comp); }, SameReplicated);
}
EI_VERIFY(0x0043F9B0, ReplicatedFinalVerify, 5, s_replicatedFinal);

// 00573740: SUB ESP,0x50; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_screenIntro)(ScreenEntry*, ScreenEntry*, void*, int, const float*, void*);
static void __fastcall ScreenIntroVerify(ScreenEntry* first, ScreenEntry* last, void* tag, int depth, const float* plane,
                                         void* manager) {
    if (!Orderable("ScreenIntroSort", first, last, plane, manager)) {
        s_screenIntro(first, last, tag, depth, plane, manager);
        return;
    }
    CompareSort(
        "ScreenIntroSort", first, last, [&](ScreenEntry* f, ScreenEntry* l) { s_screenIntro(f, l, tag, depth, plane, manager); },
        [&](ScreenEntry* f, ScreenEntry* l) { ScreenIntroSort(f, l, tag, depth, plane, manager); }, SameScreen);
}
EI_VERIFY(0x00573740, ScreenIntroVerify, 5, s_screenIntro);

// 00575BC0: SUB ESP,0x34; CMP ECX,EDX (5 bytes; the JE after them sees the flags).
static void(__fastcall* s_screenInsertion)(ScreenEntry*, ScreenEntry*, const float*, void*);
static void __fastcall ScreenInsertionVerify(ScreenEntry* first, ScreenEntry* last, const float* plane, void* manager) {
    if (!Orderable("ScreenInsertionSort", first, last, plane, manager)) {
        s_screenInsertion(first, last, plane, manager);
        return;
    }
    CompareSort(
        "ScreenInsertionSort", first, last, [&](ScreenEntry* f, ScreenEntry* l) { s_screenInsertion(f, l, plane, manager); },
        [&](ScreenEntry* f, ScreenEntry* l) { ScreenInsertionSort(f, l, plane, manager); }, SameScreen);
}
EI_VERIFY(0x00575BC0, ScreenInsertionVerify, 5, s_screenInsertion);

// 00576D40: SUB ESP,0x28; PUSH EBP; MOV EBP,ECX (6 bytes). Unguarded (see CompareUnguarded).
static void(__fastcall* s_screenUnguarded)(ScreenEntry*, ScreenEntry*, void*, const float*, void*);
static void __fastcall ScreenUnguardedVerify(ScreenEntry* first, ScreenEntry* last, void* tag, const float* plane,
                                             void* manager) {
    if (last < first || !Orderable("ScreenUnguardedInsertionSort", first - kGuard, last, plane, manager)) {
        s_screenUnguarded(first, last, tag, plane, manager);
        return;
    }
    CompareUnguarded(
        "ScreenUnguardedInsertionSort", first, last,
        [&](ScreenEntry* f, ScreenEntry* l) { s_screenUnguarded(f, l, tag, plane, manager); },
        [&](ScreenEntry* f, ScreenEntry* l) { ScreenUnguardedInsertionSort(f, l, tag, plane, manager); }, SameScreen);
}
EI_VERIFY(0x00576D40, ScreenUnguardedVerify, 6, s_screenUnguarded);

// 0060B1E0: PUSH EBX; PUSH EBP; PUSH ESI; MOV EBX,EDX (5 bytes).
static void(__fastcall* s_idIntro)(int*, int*, const int*, int);
static void __fastcall IdIntroVerify(int* first, int* last, const int* tag, int depth) {
    CompareSort(
        "IdIntroSort", first, last, [&](int* f, int* l) { s_idIntro(f, l, tag, depth); },
        [&](int* f, int* l) { IdIntroSort(f, l, tag, depth); }, [](int a, int b) { return a == b; });
}
EI_VERIFY(0x0060B1E0, IdIntroVerify, 5, s_idIntro);

// 0064B750: PUSH ECX; MOV EAX,EDX; PUSH EBX; SUB EAX,ECX (6 bytes; the AND after them sets the
// flags again).
static void(__fastcall* s_idFinal)(int*, int*);
static void __fastcall IdFinalVerify(int* first, int* last) {
    CompareSort(
        "IdFinalSort", first, last, [&](int* f, int* l) { s_idFinal(f, l); }, [&](int* f, int* l) { IdFinalSort(f, l); },
        [](int a, int b) { return a == b; });
}
EI_VERIFY(0x0064B750, IdFinalVerify, 6, s_idFinal);

// 0061EF60: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,EDX (6 bytes).
static void(__fastcall* s_serverIntro)(MpAddressEntry*, MpAddressEntry*, MpAddressEntry*, int, int);
static void __fastcall ServerIntroVerify(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry* tag, int depth, int key) {
    CompareSort(
        "ServerIntroSort", first, last, [&](MpAddressEntry* f, MpAddressEntry* l) { s_serverIntro(f, l, tag, depth, key); },
        [&](MpAddressEntry* f, MpAddressEntry* l) { ServerIntroSort(f, l, tag, depth, key); }, SameServer);
}
EI_VERIFY(0x0061EF60, ServerIntroVerify, 6, s_serverIntro);

// 0061F6F0: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,EDX (6 bytes).
static void(__fastcall* s_serverInsertion)(MpAddressEntry*, MpAddressEntry*, int);
static void __fastcall ServerInsertionVerify(MpAddressEntry* first, MpAddressEntry* last, int key) {
    CompareSort(
        "ServerInsertionSort", first, last, [&](MpAddressEntry* f, MpAddressEntry* l) { s_serverInsertion(f, l, key); },
        [&](MpAddressEntry* f, MpAddressEntry* l) { ServerInsertionSort(f, l, key); }, SameServer);
}
EI_VERIFY(0x0061F6F0, ServerInsertionVerify, 6, s_serverInsertion);

// 0061FB00: PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,EDX (5 bytes). Unguarded (see CompareUnguarded).
static void(__fastcall* s_serverUnguarded)(MpAddressEntry*, MpAddressEntry*, MpAddressEntry*, int);
static void __fastcall ServerUnguardedVerify(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry* tag, int key) {
    if (last < first) {
        s_serverUnguarded(first, last, tag, key);
        return;
    }
    CompareUnguarded(
        "ServerUnguardedInsertionSort", first, last,
        [&](MpAddressEntry* f, MpAddressEntry* l) { s_serverUnguarded(f, l, tag, key); },
        [&](MpAddressEntry* f, MpAddressEntry* l) { ServerUnguardedInsertionSort(f, l, tag, key); }, SameServer);
}
EI_VERIFY(0x0061FB00, ServerUnguardedVerify, 5, s_serverUnguarded);

// ---- Objectives quests --------------------------------------------------------------------------

namespace {

bool HasRecords(const mfc::CString* first, const mfc::CString* last) {
    for (const mfc::CString* p = first; p != last; ++p)
        if (!TheQuestTable()->Find(*p)) return false;
    return true;
}

// `guard` elements before `first` belong to the copy (the unguarded sort reads them); `runsOff`:
// a name without a record lets this one's scans leave the range.
template <typename Original, typename Ours>
void CompareQuests(const char* name, mfc::CString* first, mfc::CString* last, int guard, bool runsOff, Original original,
                   Ours ours) {
    if (last < first) {
        original(first, last);
        return;
    }
    std::vector<mfc::CString> input(first - guard, last);
    ours(first, last);
    if (runsOff && !HasRecords(input.data(), input.data() + input.size())) {
        static int s_logged = 0;
        if (s_logged < 10) {
            ++s_logged;
            ei::Log("%s: %d quests, one without a record: ours only", name, static_cast<int>(last - first));
        }
        return;
    }
    original(input.data() + guard, input.data() + input.size());
    int mismatch = -1;
    for (int i = 0; i < static_cast<int>(input.size()); ++i)
        if (static_cast<const char*>(input[static_cast<std::size_t>(i)]) != static_cast<const char*>(first[i - guard])) {
            mismatch = i - guard;
            break;
        }
    ei::verify::Check(name, -1, mismatch, "%d quests, first difference at %d", static_cast<int>(last - first), mismatch);
}

// ---- Save list ------------------------------------------------------------------------------------

// What a save entry holds, by identity: its strings (copies share them), time, game time bits,
// flag and each array's strings.
struct SavePrint {
    const char* strings[4];
    std::int32_t savedAt;
    std::uint32_t gameTime;
    bool broken;
    std::vector<const char*> arrays[2];

    static SavePrint Of(const SaveEntry& e) {
        SavePrint p{{e.title_, e.folder_, e.map_, e.zone_}, e.savedAt_, 0, e.broken_, {}};
        std::memcpy(&p.gameTime, &e.gameTime_, sizeof(p.gameTime));
        for (int a = 0; a < 2; ++a)
            for (int i = 0; i < e.stringArrays_[a].size; ++i)
                p.arrays[a].push_back(static_cast<const char*>(e.stringArrays_[a].data[i]));
        return p;
    }
    bool operator==(const SavePrint& o) const {
        return std::memcmp(strings, o.strings, sizeof(strings)) == 0 && savedAt == o.savedAt && gameTime == o.gameTime &&
               broken == o.broken && arrays[0] == o.arrays[0] && arrays[1] == o.arrays[1];
    }
};

// Entries holding only a saved time and a tag (in the game time): empty strings and arrays, so
// copying, assigning and destroying them allocate nothing.
struct KeyEntries {
    std::vector<SaveEntry> entries;

    explicit KeyEntries(std::size_t n) : entries(n) {
        for (SaveEntry& e : entries) {
            new (&e.title_) mfc::CString();
            new (&e.folder_) mfc::CString();
            new (&e.map_) mfc::CString();
            new (&e.zone_) mfc::CString();
            e.stringArrays_[0].Construct();
            e.stringArrays_[1].Construct();
            e.broken_ = false;
        }
    }
    ~KeyEntries() {
        for (SaveEntry& e : entries) e.Destruct();
    }
    KeyEntries(const KeyEntries&) = delete;
    KeyEntries& operator=(const KeyEntries&) = delete;

    void Set(std::size_t i, std::int32_t savedAt, int tag) {
        entries[i].savedAt_ = savedAt;
        entries[i].gameTime_ = static_cast<float>(tag);
    }
    int Tag(std::size_t i) const { return static_cast<int>(entries[i].gameTime_); }
    SaveEntry* Data() { return entries.data(); }
};

bool DistinctTimes(const std::vector<SavePrint>& entries) {
    std::vector<std::int32_t> times;
    for (const SavePrint& e : entries) times.push_back(e.savedAt);
    std::sort(times.begin(), times.end());
    return std::adjacent_find(times.begin(), times.end()) == times.end();
}

// `runsOff`: the call's unguarded scans can leave the range when two saves have the same time.
template <typename Original, typename Ours>
void CompareSaves(const char* name, SaveEntry* first, SaveEntry* last, int guard, bool runsOff, Original original,
                  Ours ours) {
    if (last < first) {
        original(first, last);
        return;
    }
    std::size_t n = static_cast<std::size_t>(last - first + guard);
    std::vector<SavePrint> input;
    for (const SaveEntry* p = first - guard; p != last; ++p) input.push_back(SavePrint::Of(*p));
    ours(first, last);
    if (runsOff && !DistinctTimes(input)) {
        static int s_logged = 0;
        if (s_logged < 10) {
            ++s_logged;
            ei::Log("%s: %d saves, two of the same time: ours only", name, static_cast<int>(last - first));
        }
        return;
    }
    KeyEntries twin(n);
    for (std::size_t i = 0; i < n; ++i) twin.Set(i, input[i].savedAt, static_cast<int>(i));
    original(twin.Data() + guard, twin.Data() + n);
    int mismatch = -1;
    for (std::size_t i = 0; i < n; ++i)
        if (!(input[static_cast<std::size_t>(twin.Tag(i))] == SavePrint::Of(first[static_cast<int>(i) - guard]))) {
            mismatch = static_cast<int>(i) - guard;
            break;
        }
    ei::verify::Check(name, -1, mismatch, "%d entries, first difference at %d", static_cast<int>(last - first), mismatch);
}

} // namespace

// 0064F310: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,EDX (5 bytes).
static void(__fastcall* s_questIntro)(mfc::CString*, mfc::CString*, void*, int, int);
static void __fastcall QuestIntroVerify(mfc::CString* first, mfc::CString* last, void* tag, int depth, int comp) {
    CompareQuests(
        "QuestIntroSort", first, last, 0, last - first > 16,
        [&](mfc::CString* f, mfc::CString* l) { s_questIntro(f, l, tag, depth, comp); },
        [&](mfc::CString* f, mfc::CString* l) { QuestIntroSort(f, l, tag, depth, comp); });
}
EI_VERIFY(0x0064F310, QuestIntroVerify, 5, s_questIntro);

// 0064F9A0: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,EDX (6 bytes). Guarded (a name without
// a record goes to the front).
static void(__fastcall* s_questInsertion)(mfc::CString*, mfc::CString*, int);
static void __fastcall QuestInsertionVerify(mfc::CString* first, mfc::CString* last, int comp) {
    CompareQuests(
        "QuestInsertionSort", first, last, 0, false, [&](mfc::CString* f, mfc::CString* l) { s_questInsertion(f, l, comp); },
        [&](mfc::CString* f, mfc::CString* l) { QuestInsertionSort(f, l, comp); });
}
EI_VERIFY(0x0064F9A0, QuestInsertionVerify, 6, s_questInsertion);

// 0064FBD0: PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,EDX (5 bytes). Unguarded (see CompareUnguarded).
static void(__fastcall* s_questUnguarded)(mfc::CString*, mfc::CString*, void*, int);
static void __fastcall QuestUnguardedVerify(mfc::CString* first, mfc::CString* last, void* tag, int comp) {
    CompareQuests(
        "QuestUnguardedInsertionSort", first, last, kGuard, true,
        [&](mfc::CString* f, mfc::CString* l) { s_questUnguarded(f, l, tag, comp); },
        [&](mfc::CString* f, mfc::CString* l) { QuestUnguardedInsertionSort(f, l, tag, comp); });
}
EI_VERIFY(0x0064FBD0, QuestUnguardedVerify, 5, s_questUnguarded);

// 00657DF0: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,EDX (6 bytes).
static void(__fastcall* s_saveIntro)(SaveEntry*, SaveEntry*, void*, int, int);
static void __fastcall SaveIntroVerify(SaveEntry* first, SaveEntry* last, void* tag, int depth, int comp) {
    CompareSaves(
        "SaveIntroSort", first, last, 0, last - first > 16, [&](SaveEntry* f, SaveEntry* l) { s_saveIntro(f, l, tag, depth, comp); },
        [&](SaveEntry* f, SaveEntry* l) { SaveIntroSort(f, l, tag, depth, comp); });
}
EI_VERIFY(0x00657DF0, SaveIntroVerify, 6, s_saveIntro);

// 00657F10: PUSH ESI; MOV ESI,EDX; SUB EDX,ECX (5 bytes; the IMUL after them sets the flags again).
static void(__fastcall* s_saveFinal)(SaveEntry*, SaveEntry*, int);
static void __fastcall SaveFinalVerify(SaveEntry* first, SaveEntry* last, int comp) {
    CompareSaves(
        "SaveFinalSort", first, last, 0, last - first > 16, [&](SaveEntry* f, SaveEntry* l) { s_saveFinal(f, l, comp); },
        [&](SaveEntry* f, SaveEntry* l) { SaveFinalSort(f, l, comp); });
}
EI_VERIFY(0x00657F10, SaveFinalVerify, 5, s_saveFinal);

// 00657D50: ours only (its assignments allocate): the entries written hold what [first, last)
// held, and the first one written comes back.
static void* s_saveCopyBackwardUnused;
static SaveEntry* __fastcall SaveCopyBackwardVerify(SaveEntry* first, SaveEntry* last, SaveEntry* result) {
    std::vector<SavePrint> source;
    for (const SaveEntry* p = first; p < last; ++p) source.push_back(SavePrint::Of(*p));
    SaveEntry* written = SaveCopyBackward(first, last, result);
    int n = static_cast<int>(source.size());
    int mismatch = written == result - n ? -1 : -2;
    for (int i = 0; i < n && mismatch == -1; ++i)
        if (!(source[static_cast<std::size_t>(i)] == SavePrint::Of(written[i]))) mismatch = i;
    ei::verify::Check("SaveCopyBackward", -1, mismatch, "%d entries, first difference at %d (-2: the result)", n, mismatch);
    return written;
}
EI_VERIFY(0x00657D50, SaveCopyBackwardVerify, 0, s_saveCopyBackwardUnused);

// ---- Briefing topics and hero names (S12f) --------------------------------------------------------

namespace {

std::string TextOf(const stlp::string& s) { return std::string(s.data(), s.size()); }

// Game strings freed last to first (the pool's lists are LIFO).
void PopAll(std::vector<stlp::string>& strings) {
    while (!strings.empty()) strings.pop_back();
}

std::vector<stlp::string> GameStrings(const std::vector<std::string>& texts) {
    std::vector<stlp::string> strings;
    strings.reserve(texts.size());
    for (const std::string& t : texts) strings.emplace_back(t.data(), t.data() + t.size());
    return strings;
}

template <typename Original, typename Ours>
void CompareTopics(const char* name, stlp::string* first, stlp::string* last, Original original, Ours ours) {
    if (last < first) {
        original(first, last);
        return;
    }
    std::vector<std::string> kept;
    kept.reserve(static_cast<std::size_t>(last - first));
    for (const stlp::string* p = first; p != last; ++p) kept.push_back(TextOf(*p));
    ours(first, last);
    std::vector<stlp::string> twin = GameStrings(kept);
    original(twin.data(), twin.data() + twin.size());
    int mismatch = -1;
    for (std::size_t i = 0; i < twin.size(); ++i)
        if (TextOf(twin[i]) != TextOf(first[i])) {
            mismatch = static_cast<int>(i);
            break;
        }
    PopAll(twin);
    ei::verify::Check(name, -1, mismatch, "%d topics, first difference at %d", static_cast<int>(kept.size()), mismatch);
}

template <typename Original, typename Ours>
void CompareNames(const char* name, mfc::CString* first, mfc::CString* last, Original original, Ours ours) {
    if (last < first) {
        original(first, last);
        return;
    }
    std::vector<mfc::CString> input(first, last);
    ours(first, last);
    original(input.data(), input.data() + input.size());
    int mismatch = -1;
    for (std::size_t i = 0; i < input.size(); ++i)
        if (static_cast<const char*>(input[i]) != static_cast<const char*>(first[i])) {
            mismatch = static_cast<int>(i);
            break;
        }
    ei::verify::Check(name, -1, mismatch, "%d names, first difference at %d", static_cast<int>(input.size()), mismatch);
}

} // namespace

// 0060B330: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,EDX (6 bytes).
static void(__fastcall* s_topicIntro)(stlp::string*, stlp::string*, int, int, int);
static void __fastcall TopicIntroVerify(stlp::string* first, stlp::string* last, int tag, int depth, int comp) {
    CompareTopics(
        "TopicIntroSort", first, last, [&](stlp::string* f, stlp::string* l) { s_topicIntro(f, l, tag, depth, comp); },
        [&](stlp::string* f, stlp::string* l) { TopicIntroSort(f, l, tag, depth, comp); });
}
EI_VERIFY(0x0060B330, TopicIntroVerify, 6, s_topicIntro);

// 0060B440: PUSH ESI; MOV ESI,EDX; SUB EDX,ECX (5 bytes).
static void(__fastcall* s_topicFinal)(stlp::string*, stlp::string*, int);
static void __fastcall TopicFinalVerify(stlp::string* first, stlp::string* last, int comp) {
    CompareTopics(
        "TopicFinalSort", first, last, [&](stlp::string* f, stlp::string* l) { s_topicFinal(f, l, comp); },
        [&](stlp::string* f, stlp::string* l) { TopicFinalSort(f, l, comp); });
}
EI_VERIFY(0x0060B440, TopicFinalVerify, 5, s_topicFinal);

// 0061A240: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,EDX (5 bytes).
static void(__fastcall* s_nameIntro)(mfc::CString*, mfc::CString*, mfc::CString*, int);
static void __fastcall NameIntroVerify(mfc::CString* first, mfc::CString* last, mfc::CString* tag, int depth) {
    CompareNames(
        "NameIntroSort", first, last, [&](mfc::CString* f, mfc::CString* l) { s_nameIntro(f, l, tag, depth); },
        [&](mfc::CString* f, mfc::CString* l) { NameIntroSort(f, l, tag, depth); });
}
EI_VERIFY(0x0061A240, NameIntroVerify, 5, s_nameIntro);

// 0061A300: PUSH EDI; MOV EDI,EDX; MOV EAX,EDI (5 bytes).
static void(__fastcall* s_nameFinal)(mfc::CString*, mfc::CString*);
static void __fastcall NameFinalVerify(mfc::CString* first, mfc::CString* last) {
    CompareNames(
        "NameFinalSort", first, last, [&](mfc::CString* f, mfc::CString* l) { s_nameFinal(f, l); },
        [&](mfc::CString* f, mfc::CString* l) { NameFinalSort(f, l); });
}
EI_VERIFY(0x0061A300, NameFinalVerify, 5, s_nameFinal);

// ---- Start-up check --------------------------------------------------------------------------------

namespace {

struct Random {
    std::uint32_t state;
    std::uint32_t Next() {
        state = state * 1103515245u + 12345u;
        return state >> 8;
    }
};

// Sorts `input` both ways (introsort with `depth`, then the final sort) and reports the first case
// whose orders differ.
template <typename T, typename Intro, typename Final, typename Same>
bool SortedAlike(const std::vector<T>& input, int depth, Intro intro, Final final, Intro ourIntro, Final ourFinal, Same same) {
    std::vector<T> theirs(input), ours(input);
    if (!input.empty()) {
        intro(theirs.data(), theirs.data() + theirs.size(), depth);
        final(theirs.data(), theirs.data() + theirs.size());
        ourIntro(ours.data(), ours.data() + ours.size(), depth);
        ourFinal(ours.data(), ours.data() + ours.size());
    }
    for (std::size_t i = 0; i < input.size(); ++i)
        if (!same(theirs[i], ours[i])) return false;
    return true;
}

int Depth(std::size_t n, std::uint32_t r) {
    int depth = 0;
    for (std::size_t k = n; k > 1; k >>= 1) ++depth;
    return (r & 3) == 0 ? static_cast<int>(r >> 2) % 3 : depth * 2;   // a quarter reach the heap sort
}

struct ChunkStorage {
    alignas(4) std::uint8_t bytes[sizeof(net::FileChunkMessage)];
};

void SortStartupCheck() {
    // A sort listed in disabled.txt has no trampoline: nothing to compare against.
    if (!s_idIntro || !s_idFinal || !s_replicatedIntro || !s_replicatedFinal || !s_chunkIntro || !s_chunkFinal ||
        !s_serverIntro || !s_serverInsertion || !s_serverUnguarded)
        return;
    int cases = 0, failed = -1;
    const char* which = "";
    std::vector<ChunkStorage> chunks(400);
    for (std::uint32_t seed = 1; seed <= 300 && failed < 0; ++seed, ++cases) {
        Random random{seed};
        std::size_t n = random.Next() % 300;
        std::uint32_t keys = 1 + random.Next() % 40;
        int depth = Depth(n, random.Next());

        std::vector<int> ids(n);
        for (int& id : ids) id = static_cast<int>(random.Next() % keys) - 5;
        using IdFn = void (*)(int*, int*, int);
        using IdFinalFn = void (*)(int*, int*);
        if (!SortedAlike<int, IdFn, IdFinalFn>(
                ids, depth, [](int* f, int* l, int d) { s_idIntro(f, l, nullptr, d); }, [](int* f, int* l) { s_idFinal(f, l); },
                [](int* f, int* l, int d) { IdIntroSort(f, l, nullptr, d); }, [](int* f, int* l) { IdFinalSort(f, l); },
                [](int a, int b) { return a == b; })) {
            failed = static_cast<int>(seed);
            which = "ids";
            break;
        }

        // Pairs with equal objects and different priorities: the order of equals shows.
        std::vector<net::Replicated> pairs(n);
        for (std::size_t i = 0; i < n; ++i) {
            pairs[i] = net::Replicated{};
            pairs[i].object = reinterpret_cast<net::Message*>(static_cast<std::uintptr_t>(0x1000 + 8 * (random.Next() % keys)));
            pairs[i].priority = static_cast<std::uint8_t>(i);
        }
        using PairFn = void (*)(net::Replicated*, net::Replicated*, int);
        using PairFinalFn = void (*)(net::Replicated*, net::Replicated*);
        if (!SortedAlike<net::Replicated, PairFn, PairFinalFn>(
                pairs, depth, [](net::Replicated* f, net::Replicated* l, int d) { s_replicatedIntro(f, l, nullptr, d, nullptr); },
                [](net::Replicated* f, net::Replicated* l) { s_replicatedFinal(f, l, nullptr); },
                [](net::Replicated* f, net::Replicated* l, int d) { ReplicatedIntroSort(f, l, nullptr, d, nullptr); },
                [](net::Replicated* f, net::Replicated* l) { ReplicatedFinalSort(f, l, nullptr); }, SameReplicated)) {
            failed = static_cast<int>(seed);
            which = "replicated";
            break;
        }

        // Chunk pointers into a scratch pool; only the index (+0x10) is read.
        std::vector<Chunk> order(n);
        for (std::size_t i = 0; i < n; ++i) {
            ChunkStorage& storage = chunks[random.Next() % chunks.size()];
            order[i] = reinterpret_cast<Chunk>(&storage);
        }
        for (ChunkStorage& storage : chunks)
            reinterpret_cast<Chunk>(&storage)->index_ = static_cast<std::uint16_t>(random.Next() % (keys * 4));
        using ChunkFn = void (*)(Chunk*, Chunk*, int);
        using ChunkFinalFn = void (*)(Chunk*, Chunk*);
        if (!SortedAlike<Chunk, ChunkFn, ChunkFinalFn>(
                order, depth, [](Chunk* f, Chunk* l, int d) { s_chunkIntro(f, l, nullptr, d, 0); },
                [](Chunk* f, Chunk* l) { s_chunkFinal(f, l, 0); }, [](Chunk* f, Chunk* l, int d) { ChunkIntroSort(f, l, nullptr, d, 0); },
                [](Chunk* f, Chunk* l) { ChunkFinalSort(f, l, 0); }, SameChunk)) {
            failed = static_cast<int>(seed);
            which = "chunks";
            break;
        }
    }
    // The server list with empty strings (copies of them allocate nothing) and every key but the
    // names'; each sorted as the screen sorts it (the loop, then the insertion sorts).
    for (std::uint32_t seed = 1; seed <= 200 && failed < 0; ++seed, ++cases) {
        Random random{seed};
        std::size_t n = random.Next() % 120;
        std::uint32_t keys = 1 + random.Next() % 12;
        int key = 3 + static_cast<int>(random.Next() % 7);   // 3..8, 9 the state
        int depth = Depth(n, random.Next());
        std::vector<MpAddressEntry> input(n);
        for (std::size_t i = 0; i < n; ++i) {
            input[i].players = static_cast<int>(random.Next() % keys);
            input[i].maxPlayers = static_cast<int>(i);
            input[i].ping = static_cast<int>(random.Next() % keys);
            input[i].address = net::NetAddress{};
            input[i].gameType = static_cast<int>(random.Next() % keys);
            input[i].foreign = false;
            input[i].state = static_cast<int>(random.Next() % keys);
            input[i].password = false;
        }
        auto sort = [&](MpAddressEntry* f, MpAddressEntry* l, bool original) {
            if (f == l) return;
            if (original) s_serverIntro(f, l, nullptr, depth, key);
            else ServerIntroSort(f, l, nullptr, depth, key);
            if (l - f > 16) {
                if (original) {
                    s_serverInsertion(f, f + 16, key);
                    s_serverUnguarded(f + 16, l, nullptr, key);
                } else {
                    ServerInsertionSort(f, f + 16, key);
                    ServerUnguardedInsertionSort(f + 16, l, nullptr, key);
                }
            } else if (original) {
                s_serverInsertion(f, l, key);
            } else {
                ServerInsertionSort(f, l, key);
            }
        };
        std::vector<MpAddressEntry> theirs(input), ours(input);
        sort(theirs.data(), theirs.data() + n, true);
        sort(ours.data(), ours.data() + n, false);
        for (std::size_t i = 0; i < n && failed < 0; ++i)
            if (!SameServer(theirs[i], ours[i])) {
                failed = static_cast<int>(seed);
                which = "servers";
            }
    }

    char detail[96];
    std::snprintf(detail, sizeof(detail), "%d cases, first failure: %s seed %d", cases, which, failed);
    ei::verify::Report("stlp::sort start-up (ids, replicated pairs, chunks, servers)", failed < 0, detail);
}
EI_STARTUP_CHECK(SortStartupCheck);

// The save list's sort as FillEntries calls it (the introsort loop, then the final insertion
// sort), on key-only entries with distinct times (300 cases) and on entries with small arrays of empty strings (40
// cases; their blocks come from the game's heap and go back with the entries): each position's
// time and tag, and with arrays their sizes and capacities, must match.
void SaveSortStartupCheck() {
    if (!s_saveIntro || !s_saveFinal) return;
    int cases = 0, failed = -1;
    for (std::uint32_t seed = 1; seed <= 340 && failed < 0; ++seed, ++cases) {
        Random random{seed};
        bool arrays = seed > 300;
        std::size_t n = random.Next() % (arrays ? 40 : 200);
        int depth = Depth(n, random.Next());
        // Distinct times (with equal ones the unguarded scans can leave the range), shuffled.
        std::vector<std::int32_t> times(n);
        for (std::size_t i = 0; i < n; ++i) times[i] = static_cast<std::int32_t>(3 * i) - 40;
        for (std::size_t i = n; i > 1; --i) std::swap(times[i - 1], times[random.Next() % i]);
        KeyEntries theirs(n), ours(n);
        for (std::size_t i = 0; i < n; ++i) {
            theirs.Set(i, times[i], static_cast<int>(i));
            ours.Set(i, times[i], static_cast<int>(i));
            if (!arrays) continue;
            for (int a = 0; a < 2; ++a) {
                int size = static_cast<int>(random.Next() % 4);
                if (size == 0) continue;
                theirs.entries[i].stringArrays_[a].SetSize(size);
                ours.entries[i].stringArrays_[a].SetSize(size);
            }
        }
        if (n != 0) {
            s_saveIntro(theirs.Data(), theirs.Data() + n, nullptr, depth, 0);
            s_saveFinal(theirs.Data(), theirs.Data() + n, 0);
            SaveIntroSort(ours.Data(), ours.Data() + n, nullptr, depth, 0);
            SaveFinalSort(ours.Data(), ours.Data() + n, 0);
        }
        for (std::size_t i = 0; i < n && failed < 0; ++i) {
            const SaveEntry& a = theirs.entries[i];
            const SaveEntry& b = ours.entries[i];
            bool same = a.savedAt_ == b.savedAt_ && theirs.Tag(i) == ours.Tag(i);
            for (int k = 0; k < 2; ++k)
                same = same && a.stringArrays_[k].size == b.stringArrays_[k].size &&
                       a.stringArrays_[k].maxSize == b.stringArrays_[k].maxSize;
            if (!same) failed = static_cast<int>(seed);
        }
    }
    char detail[64];
    std::snprintf(detail, sizeof(detail), "%d cases, first failure: seed %d", cases, failed);
    ei::verify::Report("stlp::sort start-up (save list)", failed < 0, detail);
}
EI_STARTUP_CHECK(SaveSortStartupCheck);

// The topics and the names as their callers sort them (the introsort loop, then the final sort),
// on random texts from a small set: texts equal but for case (the topics' order of equals), the
// characters between 'Z' and 'a' (_stricmp lowers before comparing) and several lengths. Topics:
// each position's text and capacity; names: each position's string (data pointer).
const char* const kSortWords[] = {"a",  "A", "ab", "AB", "aB", "abc", "Abc", "b",  "B", "zeta", "Zeta", "_x", "[", "\x60",
                                  "Z", "y_", "Y[", "long topic text", "LONG TOPIC TEXT", ""};

std::vector<std::string> RandomWords(Random& random, std::size_t n) {
    constexpr std::uint32_t kWords = sizeof(kSortWords) / sizeof(kSortWords[0]);
    std::vector<std::string> texts(n);
    for (std::string& t : texts) {
        t = kSortWords[random.Next() % kWords];
        if (random.Next() % 4 == 0) t += static_cast<char>('0' + random.Next() % 3);
    }
    return texts;
}

void TopicNameStartupCheck() {
    if (!s_topicIntro || !s_topicFinal || !s_nameIntro || !s_nameFinal) return;
    int cases = 0, failed = -1;
    const char* which = "";
    for (std::uint32_t seed = 1; seed <= 200 && failed < 0; ++seed, ++cases) {
        Random random{seed};
        std::size_t n = random.Next() % 120;
        int depth = Depth(n, random.Next());
        std::vector<std::string> texts = RandomWords(random, n);

        std::vector<stlp::string> theirs = GameStrings(texts), ours = GameStrings(texts);
        if (n != 0) {
            s_topicIntro(theirs.data(), theirs.data() + n, 0, depth, 0);
            s_topicFinal(theirs.data(), theirs.data() + n, 0);
            TopicIntroSort(ours.data(), ours.data() + n, 0, depth, 0);
            TopicFinalSort(ours.data(), ours.data() + n, 0);
        }
        for (std::size_t i = 0; i < n && failed < 0; ++i) {
            const stlp::string& a = theirs[i];
            const stlp::string& b = ours[i];
            if (TextOf(a) != TextOf(b) || stlp::StringAbi::EndOfStorage(a) - stlp::StringAbi::Start(a) !=
                                              stlp::StringAbi::EndOfStorage(b) - stlp::StringAbi::Start(b)) {
                failed = static_cast<int>(seed);
                which = "topics";
            }
        }
        PopAll(ours);
        PopAll(theirs);
        if (failed >= 0) break;

        std::vector<mfc::CString> names;
        names.reserve(n);
        for (const std::string& t : texts) names.emplace_back(t.c_str());
        std::vector<mfc::CString> sorted(names);
        if (n != 0) {
            s_nameIntro(names.data(), names.data() + n, nullptr, depth);
            s_nameFinal(names.data(), names.data() + n);
            NameIntroSort(sorted.data(), sorted.data() + n, nullptr, depth);
            NameFinalSort(sorted.data(), sorted.data() + n);
        }
        for (std::size_t i = 0; i < n && failed < 0; ++i)
            if (static_cast<const char*>(names[i]) != static_cast<const char*>(sorted[i])) {
                failed = static_cast<int>(seed);
                which = "names";
            }
    }
    char detail[96];
    std::snprintf(detail, sizeof(detail), "%d cases, first failure: %s seed %d", cases, which, failed);
    ei::verify::Report("stlp::sort start-up (briefing topics, hero names)", failed < 0, detail);
}
EI_STARTUP_CHECK(TopicNameStartupCheck);

} // namespace
