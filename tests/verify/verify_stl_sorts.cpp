// Checks for the STLport sort instantiations over game elements (src/game/stl_sorts.cpp), all
// against the original: it sorts a copy of the range, ours the range itself, and the two orders
// are compared element by element (sort is unstable: equal keys must land in the same order). The
// original's recursive calls land on these wrappers again, so every recursion level is checked.
// Nothing here allocates on the game's heap (the copies live in the DLL's). The suite sorts short
// ranges only (the hits and pairs stay under 17), so a start-up check sorts random arrays with many
// equal keys through the originals and ours, as the callers do (introsort loop, then the final
// insertion sort), including depth limits that reach the heap sort.
#include "verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/game/map_object.h"
#include "../../src/game/net_messages.h"
#include "../../src/game/stl_sorts.h"
#include "../../src/hook/log.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
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

} // namespace
