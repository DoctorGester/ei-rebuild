// The heap-order check (EI_HEAPORDER=1, verify build on the virtual clock; ticket SV2h3).
//
// A verify wrapper runs the original and ours; the game's heap must end up as the original alone
// leaves it, or later blocks get other addresses than in a --no-hooks run and the game, which reads
// addresses, takes another course (SV2f). This finds the wrappers whose two runs break that. A thunk
// in front of every verify wrapper and of its trampoline calls it with a copy of its arguments and
// notes when it is entered and left (any calling convention: what the callee takes off the copy is
// taken off the caller's stack too), and the reservation's calls on the main thread
// (harness_heap.cpp) are kept while a wrapper runs. When a wrapper that called its trampoline
// returns, its calls are split into the original's (inside the trampoline call) and the rest (ours
// and the check's own copies), and three patterns are counted:
//
//   hole  a block the original made is freed outside it (a snapshot put back) while a block made
//         outside the original, which stays, was made in its lifetime: in a run of the original
//         alone the kept block would sit where the freed one was.
//   kept  a block made outside the original (a game-heap copy of the input) is alive while a block
//         the original makes, which stays, is made, and is freed afterwards.
//   both  blocks of both sides stay, and ours that stay are not all remakes of the original's that
//         were freed outside it (a snapshot put back before ours ran, which is harmless): one side's
//         are extra (leaked, or both sides keep something).
//
// Blocks a wrapper makes and frees before the kept side allocates do not count: freeing them puts
// the heap back as it was. Pools inside the game (CString's CFixedAlloc, the STLport node allocator,
// MFC plexes) are not seen, only their refills. heaporder.txt lists every wrapper with a pattern.
// Diagnostic only: the thunks' frames move the stack, so a run need not follow its golden.
#include "harness_heap.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ei::verify::heap {

namespace {

struct Site {
    const char* name;
    std::uintptr_t address;
    void* wrapper;
    void* trampoline;
    unsigned calls;   // main-thread calls that ran the trampoline
    unsigned holes;
    unsigned kept;
    unsigned both;
    char example[3][320];
};

// A thunk's call of a wrapper or trampoline, keyed by the thunk's frame (its EBP): frames nest
// strictly, so calls an exception unwound past are found by their keys when an outer one returns.
struct Frame {
    std::uintptr_t key;
    Site* site;
    bool trampoline;        // a call of the trampoline, not of the wrapper
    bool capable;           // a trampoline call, or a wrapper that has a trampoline
    unsigned generation;    // main thread: g_generation when entered
    unsigned start;         // main thread: g_events->size() when entered
    unsigned intervals;     // main thread's wrapper frames: their first entry in g_intervals
};

constexpr int kMaxFrames = 512;
thread_local Frame t_frames[kMaxFrames];
thread_local int t_depth;

struct Event {
    char op;
    unsigned bytes;
    const void* in;
    const void* out;
    unsigned callers[kOrderCallers];
};

std::vector<Event>* g_events;   // the main thread's heap calls while a wrapper runs
// The trampoline calls of the main thread's wrapper frames, as event ranges: a wrapper frame owns
// the entries from its `intervals` on (its callees' are dropped when they return).
std::vector<std::pair<unsigned, unsigned>>* g_intervals;
std::vector<Site*>* g_sites;
unsigned char* g_code;
unsigned g_codeUsed;
constexpr unsigned kCodeSize = 1u << 20;
unsigned g_exits;   // main-thread wrapper calls that returned
// The main thread's open frames that may call a trampoline: events are kept only while there are
// any (a wrapper without one, such as the world tick's, never compares). The records are cleared
// when none are left or when they grow past kMaxEvents (a frame from before that, of an older
// generation, is then not analysed).
int g_capable;
unsigned g_generation;
constexpr std::size_t kMaxEvents = 1u << 22;
bool g_overflowLogged;
bool g_unwindLogged;

void ClearRecords() {
    g_events->clear();
    g_intervals->clear();
    ++g_generation;
}

void Observe(char op, unsigned bytes, const void* in, const void* out, const unsigned* callers) {
    if (g_capable <= 0) return;
    if (g_events->size() >= kMaxEvents) {
        Log("heap order: more than %u heap calls inside one wrapper call, the open ones are not analysed", unsigned(kMaxEvents));
        ClearRecords();
    }
    Event e{op, bytes, in, out, {}};
    std::memcpy(e.callers, callers, sizeof(e.callers));
    g_events->push_back(e);
}

bool Inside(const std::vector<std::pair<unsigned, unsigned>>& ranges, std::size_t first, unsigned i) {
    for (std::size_t r = first; r < ranges.size(); ++r)
        if (i >= ranges[r].first && i < ranges[r].second) return true;
    return false;
}

// The first time: `what`, then the two blocks' sizes and callers (events a and b).
void Flag(Site* site, int kind, unsigned& counter, const char* what, const Event& a, const Event* b) {
    if (counter++) return;
    char* out = site->example[kind];
    const std::size_t size = sizeof(site->example[kind]);
    int n = std::snprintf(out, size, "%s: %u", what, a.bytes);
    for (unsigned c : a.callers)
        if (c && n > 0 && std::size_t(n) < size) n += std::snprintf(out + n, size - n, " %08X", c);
    if (!b || n <= 0 || std::size_t(n) >= size) return;
    n += std::snprintf(out + n, size - n, " / %u", b->bytes);
    for (unsigned c : b->callers)
        if (c && n > 0 && std::size_t(n) < size) n += std::snprintf(out + n, size - n, " %08X", c);
}

// A wrapper frame's calls: events from `start` on, its trampoline calls from g_intervals[first] on.
void Analyse(Site* site, unsigned start, std::size_t first) {
    const std::vector<Event>& events = *g_events;
    const std::vector<std::pair<unsigned, unsigned>>& ranges = *g_intervals;
    if (first >= ranges.size()) return;   // the original did not run
    ++site->calls;
    struct Life {
        unsigned made, freed, bytes;
        bool original;
    };
    constexpr unsigned kAlive = ~0u;
    std::vector<Life> lives;
    std::unordered_map<const void*, std::size_t> current;
    for (unsigned i = start; i < events.size(); ++i) {
        const Event& e = events[i];
        if (e.in) {
            auto it = current.find(e.in);
            if (it != current.end()) {
                lives[it->second].freed = i;
                current.erase(it);
            }
        }
        if (e.out) {
            current[e.out] = lives.size();
            lives.push_back(Life{i, kAlive, e.bytes, Inside(ranges, first, i)});
        }
    }
    std::vector<unsigned> keptOurs, keptOriginal;   // when each block that stays was made
    for (const Life& life : lives)
        if (life.freed == kAlive) (life.original ? keptOriginal : keptOurs).push_back(life.made);
    auto madeBetween = [](const std::vector<unsigned>& made, unsigned from, unsigned to) {
        auto it = std::upper_bound(made.begin(), made.end(), from);
        return it != made.end() && *it < to ? *it : kAlive;
    };
    bool hole = false, kept = false;
    for (const Life& life : lives) {
        if (life.freed == kAlive) continue;
        if (!hole && life.original && !Inside(ranges, first, life.freed)) {
            const unsigned k = madeBetween(keptOurs, life.made, life.freed);
            if (k != kAlive) {
                hole = true;
                Flag(site, 0, site->holes, "hole (the original's block / ours kept)", events[life.made], &events[k]);
            }
        }
        if (!kept && !life.original) {
            const unsigned k = madeBetween(keptOriginal, life.made, life.freed);
            if (k != kAlive) {
                kept = true;
                Flag(site, 1, site->kept, "kept (a copy / the original's kept)", events[life.made], &events[k]);
            }
        }
    }
    if (keptOurs.empty() || keptOriginal.empty()) return;
    // Ours that stay, less one of the original's blocks freed outside it of the same size each.
    std::unordered_map<unsigned, int> dropped;
    for (const Life& life : lives)
        if (life.original && life.freed != kAlive && !Inside(ranges, first, life.freed)) ++dropped[life.bytes];
    unsigned extra = kAlive, extras = 0;
    for (unsigned made : keptOurs) {
        int& n = dropped[events[made].bytes];
        if (n > 0) {
            --n;
        } else {
            ++extras;
            if (extra == kAlive) extra = made;
        }
    }
    if (!extras) return;
    char what[96];
    std::snprintf(what, sizeof(what), "both (%u of the original's stay, %u of ours more than it dropped; ours / the original's)",
                  unsigned(keptOriginal.size()), extras);
    Flag(site, 2, site->both, what, events[extra], &events[keptOriginal.front()]);
}

// The thunk's frame (Enter below): [ebp+4] the site (bit 0: the trampoline), [ebp+8] the caller's
// return address, its arguments from [ebp+12]; [ebp-4] what to call, [ebp-8] the stack pointer after
// the call, below that the arguments' copy.
constexpr std::uintptr_t kArgBytes = 256;

// A main-thread frame ends: its intervals go (a wrapper's), and the records when it was the last
// frame that could compare.
void Leave(const Frame& f) {
    if (!f.trampoline && f.generation == g_generation && f.intervals < g_intervals->size()) g_intervals->resize(f.intervals);
    if (f.capable && --g_capable == 0) ClearRecords();
}

// Drops the calls an exception unwound past (deeper than `key`).
void DropUnwound(std::uintptr_t key, bool main) {
    while (t_depth > 0 && t_frames[t_depth - 1].key < key) {
        const Frame& f = t_frames[--t_depth];
        if (main) Leave(f);
    }
}

void __stdcall OnEnter(std::uintptr_t ebp) {
    const auto tagged = *reinterpret_cast<const std::uintptr_t*>(ebp + 4);
    Site* site = reinterpret_cast<Site*>(tagged & ~std::uintptr_t(1));
    const bool trampoline = (tagged & 1) != 0;
    *reinterpret_cast<void**>(ebp - 4) = trampoline ? site->trampoline : site->wrapper;
    // The arguments' copy: as many bytes as the stack holds above them, at most kArgBytes.
    const auto top = reinterpret_cast<std::uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    const std::uintptr_t from = ebp + 12;
    std::memcpy(reinterpret_cast<void*>(ebp - 8 - kArgBytes), reinterpret_cast<const void*>(from),
                (std::min)(kArgBytes, top - from));
    const bool main = GetCurrentThreadId() == MainThread();
    DropUnwound(ebp, main);
    if (t_depth >= kMaxFrames) {
        if (!g_overflowLogged) Log("heap order: more than %d nested wrapper calls, the deeper ones are not seen", kMaxFrames);
        g_overflowLogged = true;
        return;
    }
    Frame& f = t_frames[t_depth++];
    f.key = ebp;
    f.site = site;
    f.trampoline = trampoline;
    f.capable = trampoline || site->trampoline;
    if (main) {
        g_capable += f.capable;
        f.generation = g_generation;
        f.start = unsigned(g_events->size());
        f.intervals = unsigned(g_intervals->size());
    }
}

// Leaves at [ebp-8] how many bytes of arguments the callee took off the stack.
void __stdcall OnExit(std::uintptr_t ebp) {
    auto& after = *reinterpret_cast<std::uintptr_t*>(ebp - 8);
    after -= ebp - 8 - kArgBytes;
    const bool main = GetCurrentThreadId() == MainThread();
    if (t_depth > 0 && t_frames[t_depth - 1].key < ebp) {
        if (!g_unwindLogged) Log("heap order: a wrapper call left by an exception (%s)", t_frames[t_depth - 1].site->name);
        g_unwindLogged = true;
        DropUnwound(ebp, main);
    }
    if (t_depth == 0 || t_frames[t_depth - 1].key != ebp) return;   // not recorded (too deep)
    const Frame f = t_frames[--t_depth];
    if (!main) return;
    if (f.generation == g_generation) {
        if (f.trampoline) {
            g_intervals->emplace_back(f.start, unsigned(g_events->size()));
        } else {
            ++g_exits;
            Analyse(f.site, f.start, f.intervals);
        }
    }
    Leave(f);
}

// Entered from a thunk, which pushed the site. The callee gets a copy of the arguments and returns
// here; then the caller's return address moves up by what the callee took off the copy, which comes
// off the caller's stack too. EAX, EDX, the other preserved registers, the flags and the x87 state (a
// float result) are kept; ECX, which no callee preserves, is not.
__declspec(naked) void Enter() {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 264                // 8 + kArgBytes
        pushfd
        pushad
        push ebp
        call OnEnter
        popad
        popfd
        call dword ptr [ebp - 4]
        mov [ebp - 8], esp
        pushfd
        pushad
        sub esp, 108
        fnsave [esp]
        push ebp
        call OnExit
        frstor [esp]
        add esp, 108
        popad
        popfd
        mov ecx, [ebp - 8]
        lea ecx, [ebp + ecx + 8]
        push dword ptr [ebp + 8]
        pop dword ptr [ecx]
        mov ebp, [ebp]
        mov esp, ecx
        ret
    }
}

// PUSH site (bit 0 set for the trampoline); JMP Enter.
void* Thunk(Site* site, bool trampoline) {
    if (g_codeUsed + 10 > kCodeSize) return nullptr;
    unsigned char* t = g_code + g_codeUsed;
    g_codeUsed += 10;
    const std::uintptr_t tagged = reinterpret_cast<std::uintptr_t>(site) | (trampoline ? 1u : 0u);
    t[0] = 0x68;
    std::memcpy(t + 1, &tagged, 4);
    t[5] = 0xE9;
    const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&Enter) - reinterpret_cast<std::uintptr_t>(t + 10));
    std::memcpy(t + 6, &rel, 4);
    return t;
}

void* InstallSite(const HookEntry& entry) {
    auto* site = new Site{entry.name, entry.address, entry.replacement, nullptr, 0, 0, 0, 0, {}};
    void* front = Thunk(site, false);
    if (!front) return entry.replacement;
    if (entry.stolen > 0 && entry.original && *entry.original) {
        site->trampoline = *entry.original;
        if (void* t = Thunk(site, true)) *entry.original = t;
    }
    g_sites->push_back(site);
    return front;
}

} // namespace

void StartOrderCheck(std::uintptr_t selfBase, std::uintptr_t selfEnd) {
    char value[8] = "";
    if (!GetEnvironmentVariableA("EI_HEAPORDER", value, sizeof(value)) || value[0] == '0') return;
    g_code = static_cast<unsigned char*>(VirtualAlloc(nullptr, kCodeSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!g_code) return;
    g_events = new std::vector<Event>;
    g_intervals = new std::vector<std::pair<unsigned, unsigned>>;
    g_sites = new std::vector<Site*>;
    SetOrderObserver(&Observe, selfBase, selfEnd);
    ei::SetVerifyInstall(&InstallSite);
    Log("harness: heap-order check on (EI_HEAPORDER): thunks in front of the verify wrappers, heaporder.txt");
}

void WriteOrderReport() {
    if (!g_sites) return;
    char path[MAX_PATH];
    ei::OutputPath("heaporder.txt", path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    unsigned ran = 0, flagged = 0;
    std::vector<Site*> sites = *g_sites;
    std::sort(sites.begin(), sites.end(), [](const Site* a, const Site* b) { return a->address < b->address; });
    for (const Site* s : sites) {
        ran += s->calls != 0;
        if (!s->holes && !s->kept && !s->both) continue;
        ++flagged;
        std::fprintf(f, "%08X %-44s calls %u hole %u kept %u both %u\n", unsigned(s->address), s->name, s->calls,
                     s->holes, s->kept, s->both);
        for (const char* example : s->example)
            if (example[0]) std::fprintf(f, "    %s\n", example);
    }
    unsigned trampolines = 0;
    for (const Site* s : sites) trampolines += s->trampoline != nullptr;
    std::fprintf(f, "# %u wrapper(s) (%u with a trampoline), %u main-thread calls; %u ran their original, %u with a pattern\n",
                 unsigned(sites.size()), trampolines, g_exits, ran, flagged);
    std::fclose(f);
}

} // namespace ei::verify::heap
