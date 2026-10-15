// ei_vectors.exe's stand-in for game.exe's data at fixed addresses (src/game/game_data.h): an image of
// game.exe (code, .rdata and .data; code holds a few addresses read as data, such as the script
// keyword table's), zero until a check writes it. Each record starts from zeros again: ResetGameData
// clears what the last one was handed, then writes back what the fixtures kept.
#include "vectors.h"

#include "game/game_data.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr std::uintptr_t kImageBegin = 0x00401000;
constexpr std::uintptr_t kImageEnd = 0x007D7000;

unsigned char g_image[kImageEnd - kImageBegin];

struct Handed {
    std::uintptr_t address;
    std::size_t size;
};
std::vector<Handed> g_handed;   // what ResetGameData clears

struct Kept {
    std::uintptr_t address;
    std::vector<unsigned char> bytes;
};
std::vector<Kept> g_kept;   // what the fixtures wrote

} // namespace

void* ei::ReplayGameData(std::uintptr_t address, std::size_t size) {
    if (address < kImageBegin || address > kImageEnd || size > kImageEnd - address) {
        std::fprintf(stderr, "ei_vectors: game data at %08X (%u bytes) is outside the stand-in\n",
                     static_cast<unsigned>(address), static_cast<unsigned>(size));
        std::abort();
    }
    bool known = false;
    for (Handed& h : g_handed) {
        if (h.address != address) continue;
        if (h.size < size) h.size = size;
        known = true;
        break;
    }
    if (!known) g_handed.push_back({address, size});
    return g_image + (address - kImageBegin);
}

void ei::vectors::ResetGameData() {
    for (const Handed& h : g_handed) std::memset(g_image + (h.address - kImageBegin), 0, h.size);
    g_handed.clear();
    for (const Kept& k : g_kept) std::memcpy(g_image + (k.address - kImageBegin), k.bytes.data(), k.bytes.size());
}

void ei::vectors::KeepGameData() {
    for (const Handed& h : g_handed) {
        const unsigned char* at = g_image + (h.address - kImageBegin);
        g_kept.push_back({h.address, std::vector<unsigned char>(at, at + h.size)});
    }
    g_handed.clear();
}
