// The renderer's ribbon draw (00501D90, virtual slot 16 of the renderer at 007AF060, vtable
// 0073D93C) with its helpers 00502A20 (indexed draw of TL vertices), 00502AC0 (one ribbon cross
// section) and 00502B90 (the direction at a point). A polyline of world points is projected (slot 13
// of the pipeline's transform object, or slot 8 with clipping flags), and every point becomes a
// pair of screen vertices offset across the line's screen direction by width * rhw * 15, with the
// texture's u running along the ribbon and v across it; consecutive pairs make two triangles. The
// ends are transparent (colour 0x00FFFFFF). Used for lightning bolts and similar strips. Buffers are
// function-local statics (007AF000..007AF05C). See docs/notes/models.md ("Renderer").
#include "clip.h"

#include "ei/hook.h"
#include "render/render_state.h"
#include "render/render.h"
#include "originals_core.h"
#include "originals_library.h"
#include "device_buffers.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// --- The statics ---------------------------------------------------------------------------------

using devicebuffers::ProjectedVertex;

// The engine's aligned arrays (lib::AlignedArray; the original's inlined SetSize calls 004ECB70,
// 00468F70, 004ECB50 and 00406B30 for the element sizes, which AlignedArray::Allocate matches).
lib::AlignedArray<TLVertex>& Strip() { return *reinterpret_cast<lib::AlignedArray<TLVertex>*>(0x007AF000); }      // TL vertices of the points
lib::AlignedArray<TLVertex>& Ribbon() { return *reinterpret_cast<lib::AlignedArray<TLVertex>*>(0x007AF010); }     // TL vertices, two per point
lib::AlignedArray<ProjectedVertex>& Projected() { return *reinterpret_cast<lib::AlignedArray<ProjectedVertex>*>(0x007AF020); }
lib::AlignedArray<std::uint16_t>& Indices() { return *reinterpret_cast<lib::AlignedArray<std::uint16_t>*>(0x007AF030); }
lib::AlignedArray<std::uint8_t>& Clipped() { return *reinterpret_cast<lib::AlignedArray<std::uint8_t>*>(0x007AF040); }    // a flag byte per point
std::uint8_t& Guards() { return *reinterpret_cast<std::uint8_t*>(0x007AF050); }
// The texture coordinates of the next cross section: u (007AF05C) and the two v values.
float& CurrentU() { return *reinterpret_cast<float*>(0x007AF05C); }
float& TopV() { return *reinterpret_cast<float*>(0x007AF058); }
float& BottomV() { return *reinterpret_cast<float*>(0x007AF054); }

// The index array's SetAtGrow (00452FC0) and Add (00504E50).
__declspec(naked) void __fastcall IndicesAdd(lib::AlignedArray<std::uint16_t>*, void*, int /*value*/) EI_ORIGINAL(0x00504E50)

// One function-local static array: constructed empty once (its guard bit), destroyed at exit.
template <typename T>
void EnsureStatic(std::uint8_t bit, lib::AlignedArray<T>& array, std::uintptr_t onExit) {
    if (Guards() & bit) return;
    array = lib::AlignedArray<T>{};
    Guards() |= bit;
    GameAtExit(reinterpret_cast<void(__cdecl*)()>(onExit));
}

void EnsureStatics() {
    EnsureStatic(1, Clipped(), 0x00502C80);
    EnsureStatic(2, Indices(), 0x00502C60);
    EnsureStatic(4, Projected(), 0x00502C40);
    EnsureStatic(8, Ribbon(), 0x00502C20);
    EnsureStatic(0x10, Strip(), 0x00502C00);
}


// --- Device calls --------------------------------------------------------------------------------

void* Device(void* renderer) { return At<void*>(At<char*>(renderer, 0x20), 0x54); }
template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}
constexpr int kSetRenderState = 20;
constexpr int kGetRenderState = 21;
constexpr int kDrawIndexedPrimitive = 26;
constexpr int kTriangleList = 4;
constexpr int kFvfTlVertex = 0x1C4;

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// --- x87 sequences ------------------------------------------------------------------------------

// The first cross section of a ribbon, inlined by the original at the line's first point: the
// direction (dx, dy) to the next point rounded to floats; vertex 1 as in EmitSection, vertex 2 moved
// by (-(float)(k*dy), k*dx) (its x offset rounded first).
__declspec(naked) void __stdcall EmitFirstSection(const TLVertex* /*first*/, const TLVertex* /*next*/, TLVertex** /*array*/,
                                                  int* /*count*/, std::uint32_t /*widthBits*/) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 10h
        push ebx
        push esi
        push edi
        mov esi, dword ptr [ebp + 8]
        mov ecx, dword ptr [ebp + 0Ch]
        fld dword ptr [ecx]
        fsub dword ptr [esi]
        fstp dword ptr [ebp - 4]
        fld dword ptr [ecx + 4]
        fsub dword ptr [esi + 4]
        fstp dword ptr [ebp - 8]
        fld dword ptr [ebp + 18h]
        fmul dword ptr [esi + 0Ch]
        fmul dword ptr ds:[0073C518h]
        fstp dword ptr [ebp - 0Ch]
        sub esp, 10h
        fld dword ptr [ebp - 8]
        fstp qword ptr [esp + 8]
        fld dword ptr [ebp - 4]
        fstp qword ptr [esp]
        mov eax, 006FD026h
        call eax
        add esp, 10h
        fdivr dword ptr [ebp - 0Ch]
        mov edx, dword ptr [ebp + 14h]
        mov eax, dword ptr [edx]
        lea ecx, [eax + 1]
        mov dword ptr [edx], ecx
        shl eax, 5
        mov edx, dword ptr [ebp + 10h]
        add eax, dword ptr [edx]
        mov ebx, eax
        mov edi, eax
        mov ecx, 8
        rep movsd
        fld dword ptr [ebp - 8]
        fmul st, st(1)
        fst dword ptr [ebp - 0Ch]
        fadd dword ptr [ebx]
        fstp dword ptr [ebx]
        fmul dword ptr [ebp - 4]
        fld dword ptr [ebx + 4]
        fsub st, st(1)
        fstp dword ptr [ebx + 4]
        mov eax, dword ptr ds:[007AF05Ch]
        mov dword ptr [ebx + 18h], eax
        mov ecx, dword ptr ds:[007AF058h]
        mov dword ptr [ebx + 1Ch], ecx
        mov edx, dword ptr [ebp + 14h]
        mov eax, dword ptr [edx]
        lea ecx, [eax + 1]
        mov dword ptr [edx], ecx
        shl eax, 5
        mov edx, dword ptr [ebp + 10h]
        add eax, dword ptr [edx]
        mov esi, dword ptr [ebp + 8]
        mov edi, eax
        mov ecx, 8
        rep movsd
        fld dword ptr [eax]
        fsub dword ptr [ebp - 0Ch]
        fstp dword ptr [eax]
        fadd dword ptr [eax + 4]
        fstp dword ptr [eax + 4]
        mov edx, dword ptr ds:[007AF05Ch]
        mov dword ptr [eax + 18h], edx
        mov ecx, dword ptr ds:[007AF054h]
        mov dword ptr [eax + 1Ch], ecx
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 14h
    }
}

// The two triangles of the last two cross sections (count - 4 .. count - 1).
void AddQuad(int count, bool firstBySetAtGrow) {
    if (firstBySetAtGrow) {
        WordsSetAtGrow(&Indices(), nullptr, Indices().size, count - 4);
        WordsSetAtGrow(&Indices(), nullptr, Indices().size, count - 3);
    } else {
        IndicesAdd(&Indices(), nullptr, count - 4);
        IndicesAdd(&Indices(), nullptr, count - 3);
    }
    IndicesAdd(&Indices(), nullptr, count - 2);
    IndicesAdd(&Indices(), nullptr, count - 2);
    IndicesAdd(&Indices(), nullptr, count - 3);
    IndicesAdd(&Indices(), nullptr, count - 1);
}

TLVertex* StripAt(int i) { return Strip().data + i; }
TLVertex** RibbonData() { return &Ribbon().data; }

} // namespace

// 00502AC0, transcribed: the cross section at `src` across `direction`: k = (width * rhw * 15) /
// hypot(dx, dy) unrounded; vertex 1 = src moved by (k*dy, -k*dx) with v at the top, vertex 2 moved
// by (-k*dy, k*dx) with v at the bottom, both with the current u. Appends both to `*array` at
// `*count`. __fastcall: src in ECX, direction in EDX.
__declspec(naked) void __fastcall EmitSection(const TLVertex* /*src*/, const float* /*direction*/, TLVertex** /*array*/,
                                              int* /*count*/, float /*width*/) {
    __asm {
        push ecx
        mov eax, dword ptr [esp + 0Ch]
        push ebx
        fld dword ptr [esp + 14h]
        mov ebx, dword ptr [eax]
        push ebp
        push esi
        mov esi, ecx
        lea ecx, [ebx + 1]
        mov ebp, edx
        mov dword ptr [eax], ecx
        mov edx, dword ptr [esp + 14h]
        fmul dword ptr [esi + 0Ch]
        push edi
        mov edi, dword ptr [edx]
        sub esp, 8
        mov dword ptr [esp + 18h], esi
        fmul dword ptr ds:[0073C518h]
        sub esp, 8
        shl ebx, 5
        add ebx, edi
        fstp dword ptr [esp + 30h]
        fld dword ptr [ebp + 4]
        fstp qword ptr [esp + 8]
        fld dword ptr [ebp]
        fstp qword ptr [esp]
        mov eax, 006FD026h
        call eax
        fdivr dword ptr [esp + 30h]
        mov ecx, 8
        mov edi, ebx
        rep movsd
        mov esi, dword ptr [esp + 20h]
        add esp, 10h
        fld st(0)
        fmul dword ptr [ebp + 4]
        fadd dword ptr [ebx]
        fstp dword ptr [ebx]
        fld st(0)
        fmul dword ptr [ebp]
        fsubr dword ptr [ebx + 4]
        fstp dword ptr [ebx + 4]
        mov eax, dword ptr ds:[007AF05Ch]
        mov dword ptr [ebx + 18h], eax
        mov ecx, dword ptr ds:[007AF058h]
        mov dword ptr [ebx + 1Ch], ecx
        mov ecx, dword ptr [esp + 1Ch]
        fld st(0)
        mov eax, dword ptr [ecx]
        lea edx, [eax + 1]
        mov dword ptr [ecx], edx
        mov ecx, dword ptr [esp + 18h]
        shl eax, 5
        mov edx, dword ptr [ecx]
        mov ecx, 8
        add eax, edx
        mov edi, eax
        rep movsd
        fmul dword ptr [ebp + 4]
        fsubr dword ptr [eax]
        fstp dword ptr [eax]
        fmul dword ptr [ebp]
        fadd dword ptr [eax + 4]
        pop edi
        pop esi
        pop ebp
        pop ebx
        fstp dword ptr [eax + 4]
        mov edx, dword ptr ds:[007AF05Ch]
        mov dword ptr [eax + 18h], edx
        mov ecx, dword ptr ds:[007AF054h]
        mov dword ptr [eax + 1Ch], ecx
        pop ecx
        ret 0Ch
    }
}

// --- 00502B90 / 00502AC0 / 00502A20 ---------------------------------------------------------------

// 00502B90. The screen direction at point `index` of `points` (TL vertices): mode 1 the segment from
// the previous point, mode 2 from the previous to the next point (the last point: the segment from
// the previous one), else (1, 0). __fastcall: out in ECX, index in EDX. Returns out.
float* __fastcall StripDirection(float* out, int index, int mode, const void* array) {
    const auto* points = static_cast<const lib::AlignedArray<TLVertex>*>(array);
    const TLVertex* v = points->data;
    if (mode == 1 || (mode == 2 && !(index < points->size - 1))) {
        out[0] = v[index].x - v[index - 1].x;
        out[1] = v[index].y - v[index - 1].y;
    } else if (mode == 2) {
        out[0] = v[index + 1].x - v[index - 1].x;
        out[1] = v[index + 1].y - v[index - 1].y;
    } else {
        out[0] = 1.0f;
        out[1] = 0.0f;
    }
    return out;
}
EI_REPLACE(0x00502B90, StripDirection);
EI_REPLACE(0x00502AC0, EmitSection);

// 00502A20 (thiscall on the device holder, renderer + 0x20). Draws an indexed triangle list of TL
// vertices under the device's critical section (007AEE78); with +0x158 set the vertices go to the
// tracker (+0xAC -> +0xC, slot 35) first. Counts vertices, triangles and calls (+0x16C..+0x174).
// Returns 1.
int __fastcall DrawTlTriangles(char* holder, void*, TLVertex* vertices, int vertexCount, std::uint16_t* indices,
                               int indexCount) {
    if (At<int>(holder, 0x158)) {
        void* tracker = At<void*>(At<char*>(holder, 0xAC), 0xC);
        ThisCall<void>(tracker, 35, vertices, vertexCount);
    }
    auto enter = *reinterpret_cast<void(__stdcall* const*)(void*)>(0x0073B1B4);
    auto leave = *reinterpret_cast<void(__stdcall* const*)(void*)>(0x0073B230);
    void* section = reinterpret_cast<void*>(0x007AEE78);
    // The new renderer (src/render) takes the draw (after the fog above); with the old path's
    // objects off it is skipped.
    if (render::TlDraw(holder, vertices, kFvfTlVertex, vertexCount, indices, indexCount, kTriangleList)) {
        enter(section);
        At<long>(holder, 8) = ComCall(At<void*>(holder, 0x54), kDrawIndexedPrimitive, kTriangleList, kFvfTlVertex, vertices,
                                      vertexCount, indices, indexCount, 0);
        leave(section);
    }
    At<int>(holder, 0x16C) += vertexCount;
    At<std::uint32_t>(holder, 0x170) += static_cast<std::uint32_t>(indexCount) / 3;
    At<int>(holder, 0x174) += 1;
    return 1;
}
EI_REPLACE(0x00502A20, DrawTlTriangles);

// 00501D90 (thiscall, RET 0x18). `points` is an array {data, count} of world points (16 bytes each:
// x, y, z and one more); `width` scales the ribbon's screen width; `mode` 0 projects, 1 projects and
// clips (points flagged by the transform object are skipped), 2 only sizes the buffers; u runs over
// `uLength` along the ribbon (0.875 at the start when it is 0), v from `vStart` over `vSize`.
// Returns the draw's result (1 when nothing is drawn).
int __fastcall DrawRibbon(char* renderer, void*, const int* points, float width, int mode, float uLength, float vStart,
                          float vSize) {
    char* pipeline = At<char*>(renderer, 8);
    void* transform = At<void*>(pipeline, 0xC);
    EnsureStatics();

    int n = points[1];
    if (n == 0) {
        Projected().RemoveAll();
        Strip().RemoveAll();
    } else {
        Projected().SetSize(n);
        Strip().SetSize(n);
    }
    Ribbon().SetSize(n * 2 + 100);
    Clipped().SetSize(n);
    // The index array is sized for the ribbon and then freed at once; the indices are appended later.
    Indices().SetSize(Ribbon().size * 3 + 0x96);
    Indices().RemoveAll();

    if (mode == 2) return 1;
    if (points[1] < 2) return 1;

    CurrentU() = 0.0f;
    float step = static_cast<float>(uLength / static_cast<double>(points[1] - 1));
    float zero = 0.0f;
    if (!(uLength < zero) && !(uLength > zero)) CurrentU() = 0.875f;   // compares equal to 0 (NaN too)
    BottomV() = vStart + vSize;
    std::memcpy(&TopV(), &vStart, 4);

    int clipped = mode;
    if (mode == 0) {
        const float* matrix = reinterpret_cast<const float*>(At<char*>(pipeline, 0x1C) + (At<int>(pipeline, 0x20) - 1) * 64);
        ThisCall<void>(transform, 13, matrix, reinterpret_cast<const void*>(points[0]), Projected().data, points[1]);
    } else {
        const float* matrix = reinterpret_cast<const float*>(At<char*>(pipeline, 0x24) + (At<int>(pipeline, 0x28) - 1) * 64);
        clipped = ThisCall<int>(transform, 8, matrix, reinterpret_cast<const void*>(points[0]), Projected().data,
                                Clipped().data, points[1]);
        if (clipped == 2) return 1;
    }

    // The projected points as TL vertices: opaque white, black specular; the ends transparent.
    for (int i = 0; i < Projected().size; ++i) {
        TLVertex* v = StripAt(i);
        std::memcpy(v, &Projected().data[i], 16);
        v->color = 0xFFFFFFFF;
        v->specular = 0xFF000000;
    }
    StripAt(0)->color = 0x00FFFFFF;
    StripAt(Projected().size - 1)->color = 0x00FFFFFF;

    int count = 0;
    std::uint32_t widthBits;
    std::memcpy(&widthBits, &width, 4);
    if (clipped == 0) {
        bool started = false;
        for (int i = 1; i < Strip().size; ++i) {
            if (!started) EmitFirstSection(StripAt(i - 1), StripAt(i), RibbonData(), &count, widthBits);
            CurrentU() = CurrentU() + step;
            float direction[2];
            StripDirection(direction, i, i == Strip().size - 1 ? 1 : 2, &Strip());
            EmitSection(StripAt(i), direction, RibbonData(), &count, width);
            started = true;
            AddQuad(count, true);
        }
    } else {
        ThisCall<void>(transform, 34, Strip().data, Strip().size);
        bool started = false;
        for (int i = 1; i < Strip().size; ++i) {
            const auto* flags = reinterpret_cast<const std::uint8_t*>(Clipped().data);
            if (flags[i - 1] && flags[i]) {
                CurrentU() = CurrentU() + step;
                started = false;
                continue;
            }
            if (!started) {
                float direction[2];
                StripDirection(direction, i, 1, &Strip());
                EmitSection(StripAt(i - 1), direction, RibbonData(), &count, width);
            }
            CurrentU() = CurrentU() + step;
            float direction[2];
            StripDirection(direction, i, i == Strip().size - 1 ? 1 : 2, &Strip());
            EmitSection(StripAt(i), direction, RibbonData(), &count, width);
            started = true;
            AddQuad(count, false);
        }
    }

    if (Indices().size == 0) return 1;
    void* device = Device(renderer);
    std::uint32_t savedSource = 0, savedDest = 0;
    ComCall(device, kGetRenderState, 0x13, &savedSource);
    ComCall(Device(renderer), kGetRenderState, 0x14, &savedDest);
    ComCall(Device(renderer), kSetRenderState, 0x13, 5);
    ComCall(Device(renderer), kSetRenderState, 0x14, 2);
    render::state::Render(0x13, 5);
    render::state::Render(0x14, 2);
    int result = DrawTlTriangles(At<char*>(renderer, 0x20), nullptr, reinterpret_cast<TLVertex*>(Ribbon().data),
                                 Ribbon().size, reinterpret_cast<std::uint16_t*>(Indices().data), Indices().size);
    ComCall(Device(renderer), kSetRenderState, 0x13, savedSource);
    ComCall(Device(renderer), kSetRenderState, 0x14, savedDest);
    render::state::Render(0x13, static_cast<std::uint32_t>(savedSource));
    render::state::Render(0x14, static_cast<std::uint32_t>(savedDest));
    return result;
}
EI_REPLACE(0x00501D90, DrawRibbon);

// 00504E50 (RET 4): the index array's Add (A-U10), registered on the template; returns the index.
EI_REPLACE_METHOD(0x00504E50, lib::AlignedArray<std::uint16_t>::Add);
