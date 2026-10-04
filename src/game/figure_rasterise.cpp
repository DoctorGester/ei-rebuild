// 005A0E80 (thiscall on a CFigureMesh, RET 0xC): feeds the mesh's triangles to a rasteriser (the
// map object shadows, map_object_shadow.cpp). The texture coordinates (+0xAC, +0xB0 of them) are
// scaled by 0073C370 to integers in a function-local static array (007C2E00, guard 007C2E10 bit
// 1, freed at exit by 005A1170; resized without keeping its contents); each triangle (+0xC4 u16
// corner indices, +0xC8 of them, corners +0xB4 of 6 bytes: position index, -, texture index)
// gets its three projected points (from `points`, 8 bytes each) and texture coordinates into
// `triangle`, which goes to the rasteriser's slot 9 when triangle+0x30 is set, else slot 8.
// Returns 4 * +0xA0. See docs/notes/models.md.
#include "core_maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

struct IntPoint {
    int x, y;
};
struct PointArray {           // the engine's aligned array: data, size, capacity, growth
    IntPoint* data;
    int size;
    int capacity;
    int growBy;
};
PointArray& TexturePoints() { return *reinterpret_cast<PointArray*>(0x007C2E00); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E10); }

__declspec(naked) int __cdecl GameAtExit(void(__cdecl*)()) EI_ORIGINAL(0x006E727B)
__declspec(naked) void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)
// 004F65B0: aligned storage for `count` 8-byte records.
__declspec(naked) IntPoint* __fastcall Alloc8(int) EI_ORIGINAL(0x004F65B0)

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// SetSize without keeping the contents.
void Resize(PointArray& a, int size) {
    if (!a.data) {
        a.data = Alloc8(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int step = a.growBy;
    if (step == 0) {
        step = a.size / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int capacity = a.capacity + step;
    if (!(size < capacity)) capacity = size;
    IntPoint* data = Alloc8(capacity);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.capacity = capacity;
    a.data = data;
    a.size = size;
}

} // namespace

#pragma float_control(precise, on, push)

int __fastcall RasteriseFigureMesh(char* mesh, void*, void* rasteriser, const void* pointsIn, std::uint8_t* triangle) {
    const auto* points = static_cast<const IntPoint*>(pointsIn);
    PointArray& uv = TexturePoints();
    if (!(Guard() & 1)) {
        Guard() |= 1;
        uv = PointArray{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005A1170));
    }
    Resize(uv, At<int>(mesh, 0xB0));
    float scale = GameFloat(0x0073C370);
    for (int i = 0; i < At<int>(mesh, 0xB0); ++i) {
        const float* source = At<float*>(mesh, 0xAC) + i * 2;
        uv.data[i].x = x87::fistp(StoreFloat(source[0] * scale));
        uv.data[i].y = x87::fistp(StoreFloat(source[1] * scale));
    }
    for (int t = 0; t < At<int>(mesh, 0xC8); t += 3) {
        for (int k = 0; k < 3; ++k) {
            std::uint16_t corner = At<std::uint16_t*>(mesh, 0xC4)[t + k];
            const std::uint16_t* record = At<std::uint16_t*>(mesh, 0xB4) + corner * 3;
            std::memcpy(triangle + k * 16, &points[record[0]], 8);
            std::memcpy(triangle + k * 16 + 8, &TexturePoints().data[record[2]], 8);
        }
        int slot = At<int>(triangle, 0x30) ? 9 : 8;
        using DrawFn = void(__fastcall*)(void*, void*, void*);
        reinterpret_cast<DrawFn>((*static_cast<void***>(rasteriser))[slot])(rasteriser, nullptr, triangle);
    }
    return At<int>(mesh, 0xA0) * 4;
}

#pragma float_control(pop)

EI_REPLACE(0x005A0E80, RasteriseFigureMesh);
