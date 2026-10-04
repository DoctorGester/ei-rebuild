// CAIMap::StampWaterPatch (005B7EF0, RET 8): adds to (or lifts from) the small-unit layer a
// pseudo unit made of the water depths around a point: the 15x15 cells from (x - 7, y - 7),
// clipped to the map (the last row and column excluded), copied into a shared 16x16 shape (a
// function-local static reference-counted object at 007C2F64, guard 007C2F68 bit 1, released at
// exit by 005B80C0; recreated when missing or detached). The pseudo unit's key is
// ((y + 0x800) << 16) + x; the point's cell is truncated from position * 1 / (0073FD18 * 0.5).
// See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_unit_layer.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

CRefObject*& Patch() { return *reinterpret_cast<CRefObject**>(0x007C2F64); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2F68); }

__declspec(naked) int __cdecl GameAtExit(void(__cdecl*)()) EI_ORIGINAL(0x006E727B)
// The unit layer's Add / Remove through their original addresses (ai_unit_layer.cpp).
__declspec(naked) void __fastcall LayerAdd(void*, void*, int, void*, int, int) EI_ORIGINAL(0x005B36B0)
__declspec(naked) void __fastcall LayerRemove(void*, void*, int, int, int) EI_ORIGINAL(0x005B3890)

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

CRefObject* NewPatch() {
    auto* object = static_cast<std::uint8_t*>(gamecrt::operator_new(0x10C));
    if (!object) return nullptr;
    *reinterpret_cast<std::uint32_t*>(object + 4) = 0;
    *reinterpret_cast<std::uint32_t*>(object) = 0x0073FD5C;
    std::memset(object + 8, 0, 0x100);
    *reinterpret_cast<float*>(object + 0x108) = -1.0f;
    return reinterpret_cast<CRefObject*>(object);
}

} // namespace

#pragma float_control(precise, on, push)

void CAIMap::StampWaterPatch(const Vector3* position, bool add) {
    double scale = static_cast<double>(GameFloat(0x0073B84C)) /
                   (static_cast<double>(GameFloat(0x0073FD18)) * GameFloat(0x0073B86C));
    int x = static_cast<int>(scale * position->x);
    int y = static_cast<int>(scale * position->y);
    if (!(Guard() & 1)) {
        Guard() |= 1;
        Patch() = nullptr;
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005B80C0));
    }
    if (!Patch() || (Patch()->flags7_ & CRefObject::kDetached)) {
        CRefObject* fresh = NewPatch();
        CRefObject* old = Patch();
        Patch() = fresh;
        if (fresh) ++fresh->refs_;
        if (old) old->Release();
    }
    int key = ((y + 0x800) << 16) + x;
    int x0 = x - 7 < 0 ? 0 : x - 7;
    int y0 = y - 7 < 0 ? 0 : y - 7;
    int x1 = width - 1 >= x + 7 ? x + 7 : width - 1;
    int y1 = height - 1 >= y + 7 ? y + 7 : height - 1;
    auto* shape = reinterpret_cast<std::uint8_t*>(Patch());
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            shape[8 + (yy - y0) * 16 + (xx - x0)] = static_cast<std::uint8_t>(waterDepth[yy][xx]);
    void* layer = unitLayerSmall;
    if (add) LayerAdd(layer, nullptr, key, Patch(), x0, y0);
    else LayerRemove(layer, nullptr, key, x0, y0);
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x005B7EF0, CAIMap::StampWaterPatch);
