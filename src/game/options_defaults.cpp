// The options' defaults (00458DA0; called by 00455BA0 before the registry is read): the subkey
// names, the video mode (640 x 480, 16 bits), the camera, shadows and lights, the paths ("." and
// ".\res\"), the map and stream names and the master server ("a3master.nival.com"). See
// docs/notes/objects.md ("Sixth round").
#include "options.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace options {

namespace {

void Set(GameOptions* self, std::size_t offset, std::uint32_t value) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(self) + offset, &value, 4);
}
void Text(GameOptions* self, std::size_t offset, std::uintptr_t text) {
    *reinterpret_cast<mfc::CString*>(reinterpret_cast<std::uint8_t*>(self) + offset) = reinterpret_cast<const char*>(text);
}

constexpr std::uint32_t kOne = 0x3F800000;     // 1.0f
constexpr std::uint32_t kHalf = 0x3F000000;    // 0.5f
constexpr std::uint32_t k100 = 0x42C80000;     // 100.0f
constexpr std::uint32_t kMinus100 = 0xC2C80000;
constexpr std::uint32_t k80 = 0x42A00000;
constexpr std::uint32_t k30 = 0x41F00000;

} // namespace

void __fastcall OptionsResetDefaults(GameOptions* self) {
    Text(self, 0x00, 0x00784B20);   // "Statistics Output Settings"
    Text(self, 0x04, 0x00784B08);   // "Game Specific Settings"
    Text(self, 0x08, 0x00784AF4);   // "Terrain Settings"
    Text(self, 0x0C, 0x00784AE0);   // "General Settings"
    Text(self, 0x10, 0x00784AD0);   // "Camera Settings"
    Text(self, 0x14, 0x00784AC0);   // "Shadow Settings"
    Text(self, 0x18, 0x00784AB0);   // "Path Settings"
    Set(self, 0x2C, 0);
    Set(self, 0x30, 0x280);
    Set(self, 0x34, 0x1E0);
    Set(self, 0x38, 0x10);
    Set(self, 0x3C, 0x10);
    Set(self, 0x40, 0);
    Set(self, 0x44, 1);
    for (std::size_t o = 0x48; o <= 0x5C; o += 4) Set(self, o, 0);
    Set(self, 0x68, 1);
    Set(self, 0x60, 0);
    Set(self, 0x64, 1);
    Set(self, 0x6C, 3);
    Set(self, 0x70, 0);
    Set(self, 0x74, 0);
    Set(self, 0x78, 0);
    Set(self, 0xB4, 1);
    Set(self, 0xB8, 1);
    Set(self, 0x7C, 0);
    Set(self, 0x80, 1);
    Set(self, 0x84, 1);
    Set(self, 0x88, 0xF);
    Set(self, 0x8C, kOne);
    Set(self, 0x90, kOne);
    Set(self, 0x94, kOne);
    for (std::size_t o = 0x9C; o <= 0xB0; o += 4) Set(self, o, 0);
    Set(self, 0xBC, 0x42040000);   // 33
    Set(self, 0xC0, 0x42840000);   // 66
    Set(self, 0xC4, 0x40000000);   // 2
    Set(self, 0xCC, 1);
    Set(self, 0xC8, 1);
    for (std::size_t o = 0xD0; o <= 0xE4; o += 4) Set(self, o, 1);
    Text(self, 0x20, 0x00784A98);   // "."
    Text(self, 0x24, 0x00784A90);   // ".\res\"
    Text(self, 0x28, 0x00784A98);
    Set(self, 0xEC, 0);
    Set(self, 0xF0, 0xFFFFFFFF);
    Set(self, 0xFC, kMinus100);
    Set(self, 0x100, 0x447A0000);   // 1000
    Set(self, 0x104, k30);
    Set(self, 0x108, 0);
    Set(self, 0x10C, 0);
    Set(self, 0x110, kMinus100);
    Set(self, 0x114, k100);
    Set(self, 0x118, 0xC2480000);   // -50
    Set(self, 0x11C, k80);
    Set(self, 0x120, k30);
    Set(self, 0x1D8, 0);
    Set(self, 0x1DC, 0x42B40000);   // 90
    Set(self, 0x124, 0x40800000);   // 4
    Set(self, 0x128, k100);
    Set(self, 0x12C, 0x40400000);   // 3
    Set(self, 0x130, k80);
    Set(self, 0x134, k30);
    Set(self, 0x138, 0x42700000);   // 60
    Set(self, 0x13C, 200);
    Set(self, 0x140, 1);
    Set(self, 0x144, kOne);
    Set(self, 0x148, kOne);
    for (std::size_t o = 0x14C; o <= 0x170; o += 0xC) {
        Set(self, o, k100);
        Set(self, o + 4, kHalf);
        if (o + 8 <= 0x16C) Set(self, o + 8, kOne);
    }
    Set(self, 0x1E0, 0x40000000);   // 2
    Set(self, 0x1E4, k100);
    Set(self, 0x1E8, 0x3ECCCCCD);   // 0.4
    Set(self, 0x1EC, 6);
    Set(self, 0x1F0, 1);
    Set(self, 0x1F4, 0);
    Set(self, 0x1F8, kHalf);
    Set(self, 0x1FC, 1);
    Set(self, 0x200, 0);
    Set(self, 0x204, 0);
    Set(self, 0x22C, 0);
    Set(self, 0x230, kOne);
    Set(self, 0x234, 0x3F6CECED);
    Set(self, 0x238, 0x3F2AAAAB);
    Set(self, 0x240, 0x3F48C8C9);
    Set(self, 0x244, kOne);
    Set(self, 0x248, kOne);
    Text(self, 0x258, 0x007846F0);
    Text(self, 0x25C, 0x007846F0);
    Set(self, 0x260, 0x42480000);   // 50
    Set(self, 0x264, 0x42B40000);   // 90
    Text(self, 0x268, 0x00784B3C);  // "a3master.nival.com"
}
EI_REPLACE(0x00458DA0, OptionsResetDefaults);

} // namespace options
