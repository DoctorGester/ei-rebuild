// Which of game.exe's .rdata and .data bytes the image reads (EI_IMAGE_TRACE=1; harness_image_trace.h, C25).
#include "harness_image_trace.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "game/game_data.h"
#include "lib/gameapi.h"

#include <windows.h>
#include <intrin.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ei::verify::image_trace {

namespace {

constexpr std::uintptr_t kBegin = ei::kMovedBegin;      // the moved range's first page
// From the page after the import table's: every call through a slot reads it, and the rest of that page
// (0073B820-0073BFFF) is left to the zero-filled trial.
constexpr std::uintptr_t kWatchFrom = (ei::kImportsEnd + 0xFFF) & ~std::uintptr_t{0xFFF};
constexpr std::uintptr_t kEnd = kBegin + 0x61000;     // the end of the sections' file bytes (the rest starts zero)
constexpr std::size_t kPage = 0x1000;
constexpr std::size_t kPages = (kEnd - kBegin) / kPage;
constexpr std::size_t kDwords = (kEnd - kBegin) / 4;
constexpr unsigned kTrapLimit = 100000;   // traps of one page before it is left open (about a second)
constexpr DWORD kTrapFlag = 0x100;

enum : unsigned char { kNotWatched, kWatched, kSeen };

std::uintptr_t g_storage;            // where kBegin is in the image
unsigned char g_state[kDwords];
unsigned g_remaining[kPages];        // watched dwords not seen yet
unsigned g_traps[kPages];
unsigned g_open[kPages];             // threads stepping an instruction with the page open
bool g_left[kPages];                 // left open for good (kTrapLimit)
CRITICAL_SECTION g_lock;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::uintptr_t g_selfBase, g_selfEnd;

// The pages a thread opened for the instruction it is stepping.
struct Stepping {
    DWORD thread;
    unsigned pages[4];
    int count;
};
Stepping g_stepping[64];

Stepping* SteppingOf(DWORD thread, bool make) {
    for (Stepping& s : g_stepping)
        if (s.thread == thread) return &s;
    if (!make) return nullptr;
    for (Stepping& s : g_stepping)
        if (s.thread == 0) {
            s.thread = thread;
            s.count = 0;
            return &s;
        }
    return nullptr;
}

void Write(const char* text) {
    DWORD written = 0;
    WriteFile(g_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}

void Protect(unsigned page, DWORD protection) {
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(g_storage + page * kPage), kPage, protection, &old);
}

// The access's line: kind, game address, the code (ei_hook.dll's as H+offset) and up to three return addresses of
// ei_hook.dll on the stack.
void Note(std::uintptr_t eip, std::uintptr_t esp, std::uintptr_t game, const char* kind) {
    char line[192];
    auto code = [](std::uintptr_t a, char* out, std::size_t size) {
        if (a >= g_selfBase && a < g_selfEnd)
            std::snprintf(out, size, "H+%X", static_cast<unsigned>(a - g_selfBase));
        else
            std::snprintf(out, size, "%08X", static_cast<unsigned>(a));
    };
    char at[24];
    code(eip, at, sizeof(at));
    int n = std::snprintf(line, sizeof(line), "%s %08X %s", kind, static_cast<unsigned>(game), at);
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(esp);
    const auto* top = static_cast<const std::uintptr_t*>(tib->StackBase);
    int callers = 0;
    for (int i = 0; i < 96 && stack + i < top && callers < 3; ++i) {
        const std::uintptr_t w = stack[i];
        if (w < g_selfBase || w >= g_selfEnd) continue;
        n += std::snprintf(line + n, sizeof(line) - n, " H+%X", static_cast<unsigned>(w - g_selfBase));
        ++callers;
    }
    std::snprintf(line + n, sizeof(line) - n, "\r\n");
    Write(line);
}

// The end of one opening of `page` (g_lock held): closed again when no thread has it open and it holds a dword not
// seen yet, or left open for good after kTrapLimit traps.
void Close(unsigned page) {
    if (--g_open[page] || g_left[page] || !g_remaining[page]) return;
    if (g_traps[page] >= kTrapLimit) {
        g_left[page] = true;
        char line[128];
        std::snprintf(line, sizeof(line), "# page %08X left open after %u traps, %u dword(s) not seen\r\n",
                      static_cast<unsigned>(kBegin + page * kPage), g_traps[page], g_remaining[page]);
        Write(line);
        return;
    }
    Protect(page, PAGE_NOACCESS);
}

// A system call that reads or writes a buffer on a closed page fails (ERROR_INVALID_USER_BUFFER) instead of trapping:
// the file calls of game.exe's import table note the buffer's dwords as the call reads or writes them (the caller as
// the code) and keep its pages open around the call.
class OpenBuffer {
public:
    OpenBuffer(const void* buffer, DWORD size, const char* kind, std::uintptr_t caller, std::uintptr_t esp) {
        const auto b = reinterpret_cast<std::uintptr_t>(buffer);
        const std::uintptr_t e = b + static_cast<std::uintptr_t>(size), end = g_storage + (kEnd - kBegin);
        if (!size || e <= g_storage || b >= end) return;
        const std::uintptr_t lo = (std::max)(b, g_storage) - g_storage, hi = (std::min)(e, end) - g_storage;
        EnterCriticalSection(&g_lock);
        for (std::uintptr_t d = lo / 4; d < (hi + 3) / 4; ++d)
            if (g_state[d] == kWatched) {
                g_state[d] = kSeen;
                --g_remaining[d * 4 / kPage];
                Note(caller, esp, kBegin + d * 4, kind);
            }
        first_ = static_cast<unsigned>(lo / kPage);
        last_ = static_cast<unsigned>((hi - 1) / kPage);
        for (unsigned page = first_; page <= last_; ++page) {
            ++g_open[page];
            Protect(page, PAGE_READWRITE);
        }
        open_ = true;
        LeaveCriticalSection(&g_lock);
    }
    ~OpenBuffer() {
        if (!open_) return;
        EnterCriticalSection(&g_lock);
        for (unsigned page = first_; page <= last_; ++page) Close(page);
        LeaveCriticalSection(&g_lock);
    }
    OpenBuffer(const OpenBuffer&) = delete;
    OpenBuffer& operator=(const OpenBuffer&) = delete;

private:
    unsigned first_ = 0, last_ = 0;
    bool open_ = false;
};

// WriteFile goes through its import slot (gameapi.h), so it can be wrapped; ReadFile is linked directly since C18's
// import slots (a read into a watched page would fail: none seen, the read buffers are on the heap or the stack).
decltype(&::WriteFile) g_writeFile;

BOOL WINAPI TraceWriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped) {
    OpenBuffer open(buffer, size, "read", reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
                    reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()));
    return g_writeFile(file, buffer, size, written, overlapped);
}

LONG CALLBACK OnAccess(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        const std::uintptr_t at = r->ExceptionInformation[1];
        if (at - g_storage >= kEnd - kBegin) return EXCEPTION_CONTINUE_SEARCH;
        const std::size_t offset = at - g_storage;
        const auto page = static_cast<unsigned>(offset / kPage);
        EnterCriticalSection(&g_lock);
        Stepping* s = SteppingOf(GetCurrentThreadId(), true);
        if (!s || s->count == 4) {   // no room to remember the page: leave it open
            g_left[page] = true;
            Protect(page, PAGE_READWRITE);
            LeaveCriticalSection(&g_lock);
            Write("# a page left open: no stepping slot\r\n");
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (g_state[offset / 4] == kWatched) {
            g_state[offset / 4] = kSeen;
            --g_remaining[page];
            Note(e->ContextRecord->Eip, e->ContextRecord->Esp, kBegin + (offset & ~std::size_t{3}),
                 r->ExceptionInformation[0] == 1 ? "write" : "read");
        }
        ++g_traps[page];
        ++g_open[page];
        s->pages[s->count++] = page;
        Protect(page, PAGE_READWRITE);
        LeaveCriticalSection(&g_lock);
        e->ContextRecord->EFlags |= kTrapFlag;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (r->ExceptionCode == EXCEPTION_SINGLE_STEP) {
        EnterCriticalSection(&g_lock);
        Stepping* s = SteppingOf(GetCurrentThreadId(), false);
        if (!s || s->count == 0) {
            LeaveCriticalSection(&g_lock);
            return EXCEPTION_CONTINUE_SEARCH;
        }
        for (int i = 0; i < s->count; ++i) Close(s->pages[i]);
        s->count = 0;
        LeaveCriticalSection(&g_lock);
        e->ContextRecord->EFlags &= ~kTrapFlag;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

bool Enabled() {
    char value[8] = "";
    return ei::Standalone() && GetEnvironmentVariableA("EI_IMAGE_TRACE", value, sizeof(value)) && value[0] == '1';
}

void Install() {
    g_storage = ei::GameAddress(kBegin);
    InitializeCriticalSection(&g_lock);
    char path[MAX_PATH];
    ei::OutputPath("image_trace.txt", path);
    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) return;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&Install), &self);
    MODULEINFO info{};
    GetModuleInformation(GetCurrentProcess(), self, &info, sizeof(info));
    g_selfBase = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    g_selfEnd = g_selfBase + info.SizeOfImage;
    const auto* words = reinterpret_cast<const std::uint32_t*>(g_storage);
    unsigned watched = 0, pages = 0;
    for (std::size_t d = (kWatchFrom - kBegin) / 4; d < kDwords; ++d)
        if (words[d]) {
            g_state[d] = kWatched;
            ++g_remaining[d * 4 / kPage];
            ++watched;
        }
    char line[160];
    for (unsigned page = 0; page < kPages; ++page) pages += g_remaining[page] != 0;
    std::snprintf(line, sizeof(line), "# image trace: %u nonzero dword(s) on %u page(s) watched; ei_hook %08X\r\n", watched,
                  pages, static_cast<unsigned>(g_selfBase));
    Write(line);
    g_writeFile = gameapi::WriteFile;
    gameapi::WriteFile = &TraceWriteFile;
    AddVectoredExceptionHandler(1, OnAccess);
    for (unsigned page = 0; page < kPages; ++page)
        if (g_remaining[page]) Protect(page, PAGE_NOACCESS);
    Log("harness: image trace (EI_IMAGE_TRACE): %u nonzero dword(s) of game.exe's data on %u page(s) watched, "
        "image_trace.txt", watched, pages);
}

} // namespace ei::verify::image_trace
