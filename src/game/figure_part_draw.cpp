// The draw of one figure part (005A7A70, thiscall on the part's scene node, RET 0x1C): the node's
// mesh (+0x1C, a CFigureMesh) is clip-tested against the view; animated meshes (type 13..16) are
// advanced to the current time; the mesh's colours are filled (flat meshes, flag 0x100) or lit
// once (flag 0x10 marks them lit): the lights are moved into the mesh's space through the inverse
// of the model matrix, the transform object (or the owner's figure) computes the colours, the
// "crown" mesh is pulled halfway to its average colour when 0079BD28 is set; then the mesh is
// drawn through the device (004E9010 / 004E9A50, with the per-vertex offsets added into a copy
// when there are any) or, when the mesh's owner (+0x98) has a figure (+0xF0), through that
// figure's own slots; the screen rectangle goes to the node (+0x20) and to `rect`. See
// docs/notes/models.md ("Figure parts").
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct LightList {            // {data, count}: light objects
    char** data;
    int count;
};

struct AlignedArray {         // the engine's aligned array: data (32-byte aligned), size, capacity, growth
    char* data;
    int size;
    int capacity;
    int growBy;
};

// STLport string (start, finish, end of storage), handled through the original members.
struct RawString {
    char* start;
    char* finish;
    char* eos;
};

// The positions with the offsets added (007C2E48, 64 bytes per block of four vertices; a
// function-local static, guard 007C2E58 bit 1, freed at exit by 005A8AE0).
AlignedArray& MovedPositions() { return *reinterpret_cast<AlignedArray*>(0x007C2E48); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E58); }

// --- Forwarders ----------------------------------------------------------------------------------

__declspec(naked) int __cdecl GameAtExit(void(__cdecl*)()) EI_ORIGINAL(0x006E727B)
__declspec(naked) void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)
__declspec(naked) char* __fastcall Alloc4(int) EI_ORIGINAL(0x00406B50)
__declspec(naked) char* __fastcall Alloc64(int) EI_ORIGINAL(0x00452E70)
// 004F16A0 CTransformPipeline::ClipTest (model.cpp): 2 = outside the view, 1 = crossing it.
__declspec(naked) int __fastcall ClipTest(void* /*pipeline*/, void*, const void* /*bounds*/) EI_ORIGINAL(0x004F16A0)
// 005A7160: the node's animation at a time (float bits). 005A7290: after the draw (the renderer).
__declspec(naked) void __fastcall Animate(char*, void*, std::uint32_t) EI_ORIGINAL(0x005A7160)
__declspec(naked) void __fastcall FinishDraw(char*, void*, void* /*renderer*/) EI_ORIGINAL(0x005A7290)
// 0068D280: a light's colours for a material and the ambient level (float bits).
__declspec(naked) void __fastcall LightForMaterial(char*, void*, char* /*material*/, std::uint32_t /*ambient*/)
    EI_ORIGINAL(0x0068D280)
// The device's mesh draws (device_mesh.cpp).
__declspec(naked) int __fastcall DrawMesh(char*, void*, const void*, int, std::uint32_t, int, std::uint32_t,
                                          std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                          std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                          std::uint32_t, int, int) EI_ORIGINAL(0x004E9010)
__declspec(naked) int __fastcall DrawMesh2(char*, void*, std::uint32_t, std::uint32_t, const void*, int, std::uint32_t,
                                           int, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                           std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                           std::uint32_t, std::uint32_t, std::uint32_t, int, int) EI_ORIGINAL(0x004E9A50)
// STLport string members: 004217E0 string(allocator), 00425080 the end-of-storage member from an
// allocator and a value, 00428F10 assign(first, last), 0042A3D0 range initialisation (first, last,
// tag), destructors 00425040 and 0040C770 (two instantiations).
__declspec(naked) void __fastcall StringDefault(RawString*, void*, const void*) EI_ORIGINAL(0x004217E0)
__declspec(naked) void __fastcall StringEndOfStorage(char**, void*, const void*, const void*) EI_ORIGINAL(0x00425080)
__declspec(naked) void __fastcall StringAssign(RawString*, void*, const char*, const char*) EI_ORIGINAL(0x00428F10)
__declspec(naked) void __fastcall StringInitRange(RawString*, void*, const char*, const char*, const void*)
    EI_ORIGINAL(0x0042A3D0)
__declspec(naked) void __fastcall StringDestroy(RawString*) EI_ORIGINAL(0x00425040)
__declspec(naked) void __fastcall StringDestroyB(RawString*) EI_ORIGINAL(0x0040C770)
// 005197B0: map<string, piece>::find on a figure's piece map (figure + 0x14): the node to *out.
__declspec(naked) void __fastcall FindPiece(void*, void*, char**, const RawString*) EI_ORIGINAL(0x005197B0)

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// The mesh's name (virtual slot 7).
const char* MeshName(char* mesh) { return ThisCall<const char*>(mesh, 7); }

// The figure's slots used here (virtual, thiscall, all with the mesh's name first).
constexpr int kFigureFlatColours = 7;
constexpr int kFigureLight = 8;
constexpr int kFigureDrawMoved = 9;
constexpr int kFigureDraw = 10;
constexpr int kFigureDrawMovedTextured = 11;
constexpr int kFigureDrawTextured = 12;
// The transform object's slots: 2 adds the sun, 40 lights the mesh.
constexpr int kTransformAddSun = 2;
constexpr int kTransformLight = 40;

// --- x87 sequences (transcribed) -----------------------------------------------------------------

// The inverse of the model matrix's upper 3x4 part (`m` row-major 4x4, the translation in column
// 3): out[0..11] three rows of four. The determinant is (t24*m8 + a*m4) + t30*m0 with the
// cofactors unrounded where the original keeps them on the FPU stack. Returns 0 (out untouched)
// when it compares equal to 0 or is NaN. __fastcall: m in ECX, out in EDX.
__declspec(naked) int __fastcall InvertModelMatrix(const float* /*m*/, float* /*out*/) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 8
        fld dword ptr [ecx + 8]
        fmul dword ptr [ecx + 24h]
        fld dword ptr [ecx + 28h]
        fmul dword ptr [ecx + 4]
        fsubp st(1), st
        fld dword ptr [ecx + 18h]
        fmul dword ptr [ecx + 4]
        fld dword ptr [ecx + 8]
        fmul dword ptr [ecx + 14h]
        fsubp st(1), st
        fstp dword ptr [ebp - 4]
        fld dword ptr [ecx + 28h]
        fmul dword ptr [ecx + 14h]
        fld dword ptr [ecx + 24h]
        fmul dword ptr [ecx + 18h]
        fsubp st(1), st
        fstp dword ptr [ebp - 8]
        fld dword ptr [ebp - 4]
        fmul dword ptr [ecx + 20h]
        fld st(1)
        fmul dword ptr [ecx + 10h]
        faddp st(1), st
        fld dword ptr [ebp - 8]
        fmul dword ptr [ecx]
        faddp st(1), st
        fcom dword ptr ds:[0073B858h]
        fnstsw ax
        test ah, 40h
        jne singular
        fdivr dword ptr ds:[0073B84Ch]
        fld dword ptr [ebp - 8]
        fmul st, st(1)
        fstp dword ptr [edx]
        fxch st(1)
        fmul st, st(1)
        fstp dword ptr [edx + 4]
        fld dword ptr [ebp - 4]
        fmul st, st(1)
        fst dword ptr [edx + 8]
        fmul dword ptr [ecx + 2Ch]
        fld dword ptr [edx]
        fmul dword ptr [ecx + 0Ch]
        faddp st(1), st
        fld dword ptr [edx + 4]
        fmul dword ptr [ecx + 1Ch]
        faddp st(1), st
        fchs
        fstp dword ptr [edx + 0Ch]
        fld dword ptr [ecx + 20h]
        fmul dword ptr [ecx + 18h]
        fld dword ptr [ecx + 28h]
        fmul dword ptr [ecx + 10h]
        fsubp st(1), st
        fmul st, st(1)
        fstp dword ptr [edx + 10h]
        fld dword ptr [ecx + 28h]
        fmul dword ptr [ecx]
        fld dword ptr [ecx + 20h]
        fmul dword ptr [ecx + 8]
        fsubp st(1), st
        fmul st, st(1)
        fstp dword ptr [edx + 14h]
        fld dword ptr [ecx + 8]
        fmul dword ptr [ecx + 10h]
        fld dword ptr [ecx]
        fmul dword ptr [ecx + 18h]
        fsubp st(1), st
        fmul st, st(1)
        fstp dword ptr [edx + 18h]
        fld dword ptr [edx + 14h]
        fmul dword ptr [ecx + 1Ch]
        fld dword ptr [edx + 18h]
        fmul dword ptr [ecx + 2Ch]
        faddp st(1), st
        fld dword ptr [edx + 10h]
        fmul dword ptr [ecx + 0Ch]
        faddp st(1), st
        fchs
        fstp dword ptr [edx + 1Ch]
        fld dword ptr [ecx + 24h]
        fmul dword ptr [ecx + 10h]
        fld dword ptr [ecx + 20h]
        fmul dword ptr [ecx + 14h]
        fsubp st(1), st
        fmul st, st(1)
        fstp dword ptr [edx + 20h]
        fld dword ptr [ecx + 20h]
        fmul dword ptr [ecx + 4]
        fld dword ptr [ecx]
        fmul dword ptr [ecx + 24h]
        fsubp st(1), st
        fmul st, st(1)
        fstp dword ptr [edx + 24h]
        fld dword ptr [ecx]
        fmul dword ptr [ecx + 14h]
        fld dword ptr [ecx + 10h]
        fmul dword ptr [ecx + 4]
        fsubp st(1), st
        fmul st, st(1)
        fstp dword ptr [edx + 28h]
        fstp st(0)
        fld dword ptr [edx + 24h]
        fmul dword ptr [ecx + 1Ch]
        fld dword ptr [edx + 20h]
        fmul dword ptr [ecx + 0Ch]
        faddp st(1), st
        fld dword ptr [edx + 28h]
        fmul dword ptr [ecx + 2Ch]
        faddp st(1), st
        fchs
        fstp dword ptr [edx + 2Ch]
        mov eax, 1
        mov esp, ebp
        pop ebp
        ret
    singular:
        fstp st(0)
        fstp st(0)
        xor eax, eax
        mov esp, ebp
        pop ebp
        ret
    }
}

// A light's position into the mesh's space: each row ((r2*z + r1*y) + r0*x) + r3, rounded once.
// __fastcall: the inverse rows in ECX, the point in EDX, out on the stack (RET 4).
__declspec(naked) void __fastcall TransformPoint(const float* /*rows*/, const float* /*point*/, float* /*out*/) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [ecx + 28h]
        fmul dword ptr [edx + 8]
        fld dword ptr [ecx + 24h]
        fmul dword ptr [edx + 4]
        faddp st(1), st
        fld dword ptr [ecx + 20h]
        fmul dword ptr [edx]
        faddp st(1), st
        fadd dword ptr [ecx + 2Ch]
        fstp dword ptr [eax + 8]
        fld dword ptr [ecx + 18h]
        fmul dword ptr [edx + 8]
        fld dword ptr [ecx + 14h]
        fmul dword ptr [edx + 4]
        faddp st(1), st
        fld dword ptr [ecx + 10h]
        fmul dword ptr [edx]
        faddp st(1), st
        fadd dword ptr [ecx + 1Ch]
        fstp dword ptr [eax + 4]
        fld dword ptr [ecx + 8]
        fmul dword ptr [edx + 8]
        fld dword ptr [ecx + 4]
        fmul dword ptr [edx + 4]
        faddp st(1), st
        fld dword ptr [ecx]
        fmul dword ptr [edx]
        faddp st(1), st
        fadd dword ptr [ecx + 0Ch]
        fstp dword ptr [eax]
        ret 4
    }
}

// The sun's direction into the mesh's space: the direction negated, no translation, with the
// original's operand order per row. __fastcall: rows in ECX, direction in EDX, out (RET 4).
__declspec(naked) void __fastcall TransformSun(const float* /*rows*/, const float* /*direction*/, float* /*out*/) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [edx]
        fchs
        fld dword ptr [edx + 4]
        fchs
        fld dword ptr [edx + 8]
        fchs
        fld st(0)
        fmul dword ptr [ecx + 28h]
        fld st(2)
        fmul dword ptr [ecx + 24h]
        faddp st(1), st
        fld st(3)
        fmul dword ptr [ecx + 20h]
        faddp st(1), st
        fstp dword ptr [eax + 8]
        fld st(0)
        fmul dword ptr [ecx + 18h]
        fld st(2)
        fmul dword ptr [ecx + 14h]
        faddp st(1), st
        fld dword ptr [ecx + 10h]
        fmul st, st(4)
        faddp st(1), st
        fstp dword ptr [eax + 4]
        fxch st(2)
        fmul dword ptr [ecx]
        fld dword ptr [ecx + 8]
        fmul st, st(3)
        faddp st(1), st
        fld dword ptr [ecx + 4]
        fmul st, st(2)
        faddp st(1), st
        fstp dword ptr [eax]
        fxch st(1)
        fstp st(0)
        fstp st(0)
        ret 4
    }
}

// The "crown" mesh: every colour's channels pulled halfway (0073CDE0) to the average colour; the
// sums accumulate (channel * (1 / count)) unrounded, the red one is kept on the FPU stack into the
// first colour and reloaded rounded for the others; the channels are rounded by FISTP, alpha kept.
// __fastcall: colours in ECX, count in EDX.
__declspec(naked) void __fastcall CrownBlend(std::uint32_t* /*colours*/, int /*count*/) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 48h
        push ebx
        push esi
        push edi
        // [ebp-4] red sum, [ebp-8] green sum, [ebp-0Ch] blue sum, [ebp-10h] 1 / count,
        // [ebp-18h] / [ebp-20h] / [ebp-28h] qword channels, [ebp-2Ch] new red, [ebp-30h] green
        // (float, then int), [ebp-34h] blue, [ebp-38h] / [ebp-3Ch] / [ebp-40h] integer channels,
        // [ebp-44h] the count.
        mov ebx, ecx
        mov esi, edx
        mov dword ptr [ebp - 44h], edx
        fld dword ptr ds:[0073B858h]
        fst dword ptr [ebp - 4]
        fld dword ptr ds:[0073B858h]
        fst dword ptr [ebp - 8]
        fld dword ptr ds:[0073B858h]
        fst dword ptr [ebp - 0Ch]
        mov dword ptr [ebp - 10h], esi
        fild dword ptr [ebp - 10h]
        test esi, esi
        fdivr dword ptr ds:[0073B84Ch]
        fstp dword ptr [ebp - 10h]
        jle empty
        xor ecx, ecx
    sum:
        mov eax, dword ptr [ebx + ecx * 4]
        mov edi, eax
        shr edi, 10h
        and edi, 0FFh
        mov dword ptr [ebp - 18h], edi
        mov dword ptr [ebp - 14h], 0
        fild qword ptr [ebp - 18h]
        mov edi, eax
        shr edi, 8
        and edi, 0FFh
        and eax, 0FFh
        fmul dword ptr [ebp - 10h]
        mov dword ptr [ebp - 20h], edi
        mov dword ptr [ebp - 1Ch], 0
        mov dword ptr [ebp - 28h], eax
        faddp st(3), st
        fild qword ptr [ebp - 20h]
        mov dword ptr [ebp - 24h], 0
        inc ecx
        fmul dword ptr [ebp - 10h]
        cmp ecx, esi
        faddp st(2), st
        fild qword ptr [ebp - 28h]
        fmul dword ptr [ebp - 10h]
        faddp st(1), st
        jl sum
        fst dword ptr [ebp - 0Ch]
        fxch st(2)
        fst dword ptr [ebp - 4]
        fxch st(2)
        fstp st(0)
        fstp dword ptr [ebp - 8]
        xor ecx, ecx
        jmp blend
    next:
        fld dword ptr [ebp - 4]
    blend:
        mov eax, dword ptr [ebx + ecx * 4]
        mov edx, eax
        mov edi, eax
        shr edx, 10h
        and edx, 0FFh
        and eax, 0FFh
        mov dword ptr [ebp - 18h], edx
        xor edx, edx
        shr edi, 8
        mov dword ptr [ebp - 14h], edx
        and edi, 0FFh
        fild qword ptr [ebp - 18h]
        mov dword ptr [ebp - 20h], edi
        mov dword ptr [ebp - 1Ch], edx
        fild qword ptr [ebp - 20h]
        mov dword ptr [ebp - 28h], eax
        mov dword ptr [ebp - 24h], edx
        fstp dword ptr [ebp - 30h]
        fild qword ptr [ebp - 28h]
        fstp dword ptr [ebp - 34h]
        fxch st(1)
        fsub st, st(1)
        fmul dword ptr ds:[0073CDE0h]
        fadd st, st(1)
        fstp dword ptr [ebp - 2Ch]
        fstp st(0)
        fld dword ptr [ebp - 8]
        fsub dword ptr [ebp - 30h]
        fmul dword ptr ds:[0073CDE0h]
        fadd dword ptr [ebp - 30h]
        fstp dword ptr [ebp - 30h]
        fld dword ptr [ebp - 0Ch]
        fsub dword ptr [ebp - 34h]
        fmul dword ptr ds:[0073CDE0h]
        fadd dword ptr [ebp - 34h]
        fstp dword ptr [ebp - 34h]
        fld dword ptr [ebp - 2Ch]
        fistp dword ptr [ebp - 38h]
        fld dword ptr [ebp - 30h]
        fistp dword ptr [ebp - 3Ch]
        fld dword ptr [ebp - 34h]
        fistp dword ptr [ebp - 40h]
        mov edi, dword ptr [ebp - 3Ch]
        mov esi, dword ptr [ebx + ecx * 4]
        mov edx, dword ptr [ebp - 38h]
        and esi, 0FF000000h
        shl edx, 8
        or edx, edi
        shl edx, 8
        or edx, esi
        mov esi, dword ptr [ebp - 40h]
        or edx, esi
        mov dword ptr [ebx + ecx * 4], edx
        inc ecx
        cmp ecx, dword ptr [ebp - 44h]
        jl next
        jmp done
    empty:
        fstp st(0)
        fstp st(0)
        fstp st(0)
    done:
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

// *target = *offset + *target (FLD offset, FADD, FSTP). __fastcall.
__declspec(naked) void __fastcall AddOffset(const float* /*offset*/, float* /*target*/) {
    __asm {
        fld dword ptr [ecx]
        fadd dword ptr [edx]
        fstp dword ptr [edx]
        ret
    }
}

// The world time: whole ticks (007AF61C, FILD) plus the fraction of the current one (007AFCA8),
// rounded to a float. __fastcall: out in ECX.
__declspec(naked) void __fastcall TickTime(float* /*out*/) {
    __asm {
        fild dword ptr ds:[007AF61Ch]
        fadd dword ptr ds:[007AFCA8h]
        fstp dword ptr [ecx]
        ret
    }
}

// --- Arrays --------------------------------------------------------------------------------------

int GrowthStep(const AlignedArray& a) {
    if (a.growBy != 0) return a.growBy;
    int step = a.size / 8;
    if (step < 4) return 4;
    if (step > 0x400) return 0x400;
    return step;
}

// SetSize without keeping the contents (the colour arrays).
void Resize(AlignedArray& a, int size, char* (__fastcall* alloc)(int)) {
    if (!a.data) {
        a.data = alloc(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int grown = GrowthStep(a) + a.capacity;
    int capacity = size < grown ? grown : size;
    char* data = alloc(capacity);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

// SetSize keeping the contents; 0 frees the block (the moved positions, 64-byte blocks).
void SetSize64(AlignedArray& a, int size) {
    if (size == 0) {
        if (a.data) {
            GameDelete(reinterpret_cast<void**>(a.data)[-1]);
            a.data = nullptr;
        }
        a.capacity = 0;
        a.size = 0;
        return;
    }
    if (!a.data) {
        a.data = Alloc64(size);
        a.capacity = size;
        a.size = size;
        return;
    }
    if (size <= a.capacity) {
        a.size = size;
        return;
    }
    int grown = GrowthStep(a) + a.capacity;
    int capacity = size < grown ? grown : size;
    char* data = Alloc64(capacity);
    std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * 64);
    GameDelete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = size;
    a.capacity = capacity;
}

void Fill(AlignedArray& a, std::uint32_t value) {
    auto* p = reinterpret_cast<std::uint32_t*>(a.data);
    for (int i = 0; i < a.size; ++i) p[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    char* node;               // this
    char* device;
    const LightList* lights;
    char* material;           // +8 flag 0x10: keep the speculars, draw flag 8; +0x50 a vector
    int alpha;
    RECT* rect;
    char* textureA;
    char* textureB;
    void* figure;             // the mesh owner's figure (+0x98 -> +0xF0), or null
    int drawFlags;            // 1 rectangle wanted, 2 crossing the view, 8 material flag 0x10

    char* Mesh() const { return At<char*>(node, 0x1C); }
    AlignedArray& Colours() const { return At<AlignedArray>(Mesh(), 0x138); }
    AlignedArray& Speculars() const { return At<AlignedArray>(Mesh(), 0x148); }
    bool Textured() const { return textureA && textureB; }
};

// An allocator argument for the string members: they read (at most) a byte through it.
const void* Allocator(Part& part) { return reinterpret_cast<char*>(&part.material) + 3; }

// Flat meshes (flag 0x100): the figure fills the colours (slot 7), else every colour is the
// mesh's +0xD4 and every specular its +0xD8 (both arrays sized 4 * +0xA8 first).
void FlatColours(Part& part) {
    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        RawString s;
        StringDefault(&s, nullptr, Allocator(part));
        StringAssign(&s, nullptr, name, name + std::strlen(name));
        char* mesh = part.Mesh();
        ThisCall<void>(part.figure, kFigureFlatColours, &s, At<std::uint32_t>(mesh, 0xD4), At<std::uint32_t>(mesh, 0xD8),
                       &part.Colours(), &part.Speculars());
        StringDestroy(&s);
        return;
    }
    Resize(part.Colours(), At<int>(part.Mesh(), 0xA8) * 4, &Alloc4);
    Resize(part.Speculars(), At<int>(part.Mesh(), 0xA8) * 4, &Alloc4);
    Fill(part.Colours(), At<std::uint32_t>(part.Mesh(), 0xD4));
    Fill(part.Speculars(), At<std::uint32_t>(part.Mesh(), 0xD8));
}

// The inverse model matrix of the last call, used when the matrix is singular (the original then
// reads stack slots it did not write, which usually hold the previous part's inverse).
float g_lastInverse[12];

// Lighting (once, flag 0x10): the lights and the sun (007AF07C) into the mesh's space, their
// colours for the material, then the figure (slot 8) or the transform object (slot 40) lights the
// mesh; the crown blend; the speculars become the alpha unless the material keeps them.
void Light(Part& part) {
    char* pipeline = At<char*>(part.device, 0xAC);
    void* transform = At<void*>(pipeline, 0xC);
    const float* model = reinterpret_cast<const float*>(At<char*>(pipeline, 0x2C) + (At<int>(pipeline, 0x30) - 1) * 64);
    InvertModelMatrix(model, g_lastInverse);
    std::uint32_t ambient = *reinterpret_cast<const std::uint32_t*>(0x0079BE9C);
    for (int i = 0; i < part.lights->count; ++i) {
        char* light = part.lights->data[i];
        float moved[3];
        TransformPoint(g_lastInverse, &At<float>(light, 0x1C), moved);
        std::memcpy(light + 0x28, moved, sizeof(moved));
        LightForMaterial(light, nullptr, part.material, ambient);
    }
    if (char* sun = *reinterpret_cast<char**>(0x007AF07C)) {
        float moved[3];
        TransformSun(g_lastInverse, &At<float>(sun, 0xD4), moved);
        std::memcpy(sun + 0x108, moved, sizeof(moved));
        LightForMaterial(sun, nullptr, part.material, ambient);
        ThisCall<void>(transform, kTransformAddSun, sun);
    }

    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        RawString s;
        StringDefault(&s, nullptr, Allocator(part));
        StringAssign(&s, nullptr, name, name + std::strlen(name));
        ThisCall<void>(part.figure, kFigureLight, &s, part.lights, part.material, part.alpha, &part.Colours(),
                       &part.Speculars());
        StringDestroyB(&s);
    } else {
        std::uint32_t vector[3];
        std::memcpy(vector, part.material + 0x50, sizeof(vector));
        char* mesh = part.Mesh();
        ThisCall<void>(transform, kTransformLight, At<void*>(mesh, 0x9C), At<void*>(mesh, 0xA4),
                       static_cast<void*>(part.Colours().data), At<void*>(mesh, 0xBC), At<int>(mesh, 0xC0),
                       static_cast<void*>(part.lights->data), part.lights->count, static_cast<void*>(vector));
    }

    if (*reinterpret_cast<const int*>(0x0079BD28) != 0 && gamecrt::stricmp(MeshName(part.Mesh()), "crown") == 0) {
        CrownBlend(reinterpret_cast<std::uint32_t*>(part.Colours().data), part.Colours().size);
    }
    if (!(At<std::uint8_t>(part.material, 8) & 0x10)) {
        Fill(part.Speculars(), (static_cast<std::uint32_t>(part.alpha) & 0xFF) << 24);
    }
    At<std::uint32_t>(part.Mesh(), 8) |= 0x10;
}

// The positions with the per-vertex offsets (+0x128, +0x12C of them) added, in 007C2E48.
const void* MovedPositionsOf(char* mesh) {
    AlignedArray& moved = MovedPositions();
    SetSize64(moved, At<int>(mesh, 0xA0));
    std::memcpy(moved.data, At<void*>(mesh, 0x9C), static_cast<std::size_t>(At<int>(mesh, 0xA0)) * 64);
    for (int i = 0; i < At<int>(mesh, 0x12C); ++i) {
        const float* offset = reinterpret_cast<const float*>(At<char*>(mesh, 0x128)) + i * 3;
        float* x = reinterpret_cast<float*>(moved.data) + (i >> 2) * 16 + (i & 3);
        AddOffset(&offset[0], &x[0]);
        AddOffset(&offset[1], &x[4]);
        AddOffset(&offset[2], &x[8]);
    }
    return moved.data;
}

// The mesh through the device. The drawn flag (0x40) is only set when a rectangle was asked for.
void DrawThroughDevice(Part& part) {
    char* mesh = part.Mesh();
    const void* positions = At<void*>(mesh, 0x9C);
    int blocks = At<int>(mesh, 0xA0);
    if (At<int>(mesh, 0x12C) != 0) positions = MovedPositionsOf(mesh);
    mesh = part.Mesh();
    auto field = [mesh](int offset) { return At<std::uint32_t>(mesh, offset); };
    int result;
    if (part.Textured()) {
        result = DrawMesh2(part.device, nullptr, At<std::uint32_t>(part.textureA, 0x5C), At<std::uint32_t>(part.textureB, 0x5C),
                           positions, blocks, field(0xA4), static_cast<int>(field(0xA8)), field(0x138), field(0x148),
                           field(0xAC), field(0xB0), field(0xB4), field(0xB8), field(0xBC), field(0xC0), field(0xC4),
                           field(0xC8), field(0xCC), field(0xD0), part.alpha, part.drawFlags);
    } else {
        result = DrawMesh(part.device, nullptr, positions, blocks, field(0xA4), static_cast<int>(field(0xA8)), field(0x138),
                          field(0x148), field(0xAC), field(0xB0), field(0xB4), field(0xB8), field(0xBC), field(0xC0),
                          field(0xC4), field(0xC8), field(0xCC), field(0xD0), part.alpha, part.drawFlags);
    }
    if (!part.rect) return;
    RECT* own = &At<RECT>(part.node, 0x20);
    if (result == 2) {
        gameapi::SetRectEmpty(own);
        gameapi::SetRectEmpty(part.rect);
        return;
    }
    gameapi::CopyRect(own, &At<RECT>(part.device, 0x15C));
    *part.rect = *own;
    At<std::uint32_t>(part.Mesh(), 8) |= 0x40;
}

// The mesh through the owner's figure: slot 11 / 9 with the offsets (+0x128), 12 / 10 without,
// the textured ones first; the rectangle from the figure's piece of that name (+0x5C).
void DrawThroughFigure(Part& part) {
    char* mesh = part.Mesh();
    bool moved = At<int>(mesh, 0x12C) != 0;
    const char* name = MeshName(mesh);
    RawString s;
    if (moved || part.Textured()) {
        StringDefault(&s, nullptr, Allocator(part));
        StringInitRange(&s, nullptr, name, name + std::strlen(name), part.material);
    } else {
        s.start = nullptr;
        s.finish = nullptr;
        int zero = 0;
        StringEndOfStorage(&s.eos, nullptr, Allocator(part), &zero);
        StringAssign(&s, nullptr, name, name + std::strlen(name));
    }
    mesh = part.Mesh();
    std::uint32_t u = At<std::uint32_t>(mesh, 0xCC);
    std::uint32_t v = At<std::uint32_t>(mesh, 0xD0);
    void* colours = &part.Colours();
    void* speculars = &part.Speculars();
    void* offsets = mesh + 0x128;
    int result;
    if (moved && part.Textured()) {
        result = ThisCall<int>(part.figure, kFigureDrawMovedTextured, &s, part.drawFlags, At<std::uint32_t>(part.textureA, 0x5C),
                               At<std::uint32_t>(part.textureB, 0x5C), colours, speculars, offsets, u, v);
    } else if (moved) {
        result = ThisCall<int>(part.figure, kFigureDrawMoved, &s, part.drawFlags, colours, speculars, offsets, u, v);
    } else if (part.Textured()) {
        result = ThisCall<int>(part.figure, kFigureDrawTextured, &s, part.drawFlags, At<std::uint32_t>(part.textureA, 0x5C),
                               At<std::uint32_t>(part.textureB, 0x5C), colours, speculars, u, v);
    } else {
        result = ThisCall<int>(part.figure, kFigureDraw, &s, part.drawFlags, colours, speculars, u, v);
    }
    StringDestroy(&s);

    if (part.rect) {
        const char* pieceName = MeshName(part.Mesh());
        RawString key;
        key.start = nullptr;
        key.finish = nullptr;
        int zero = 0;
        StringEndOfStorage(&key.eos, nullptr, Allocator(part), &zero);
        StringAssign(&key, nullptr, pieceName, pieceName + std::strlen(pieceName));
        char* node = nullptr;
        FindPiece(static_cast<char*>(part.figure) + 0x14, nullptr, &node, &key);
        RECT* own = &At<RECT>(part.node, 0x20);
        gameapi::CopyRect(own, &At<RECT>(node, 0x5C));
        StringDestroy(&key);
        *part.rect = *own;
    }
    if (result != 2) At<std::uint32_t>(part.Mesh(), 8) |= 0x40;
}

} // namespace

// 005A7A70 (thiscall on the part's node, RET 0x1C). `lights` {data, count} of light objects,
// `material` the drawing context (+8 flags, +0x50 a vector), `alpha` the low byte for the
// speculars, `rect` (optional) receives the screen rectangle, the textures (+0x5C) select the
// two-texture draw. Returns 1.
int __fastcall DrawFigurePart(char* node, void*, char* device, const void* lights, char* material, int alpha,
                              RECT* rect, char* textureA, char* textureB) {
    char* mesh = At<char*>(node, 0x1C);
    At<std::uint32_t>(mesh, 8) &= ~0x40u;
    if (At<std::uint32_t>(At<char*>(node, 0x1C), 8) & 0x80) return 1;
    void* owner = At<void*>(At<char*>(node, 0x1C), 0x98);
    Part part{node, device, static_cast<const LightList*>(lights), material, alpha, rect, textureA, textureB,
              owner ? At<void*>(owner, 0xF0) : nullptr, 0};
    if (!(Guard() & 1)) {
        Guard() |= 1;
        MovedPositions() = AlignedArray{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005A8AE0));
    }

    int visibility = ClipTest(At<void*>(device, 0xAC), nullptr, part.Mesh() + 0x188);
    if (visibility == 2) return 1;
    int type = At<int>(part.Mesh(), 0x15C);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        At<std::uint32_t>(node, 8) |= *reinterpret_cast<const std::uint32_t*>(0x00790C0C);
        std::uint32_t bits;
        std::memcpy(&bits, &time, 4);
        Animate(node, nullptr, bits);
    }

    std::uint32_t meshFlags = At<std::uint32_t>(part.Mesh(), 8);
    if (meshFlags & 0x100) {
        FlatColours(part);
    } else if (!(meshFlags & 0x10)) {
        Light(part);
    }

    if (visibility == 1) part.drawFlags = 2;
    if (rect) part.drawFlags |= 1;
    if (At<std::uint8_t>(material, 8) & 0x10) part.drawFlags |= 8;
    if (part.figure) {
        DrawThroughFigure(part);
    } else {
        DrawThroughDevice(part);
    }
    FinishDraw(node, nullptr, reinterpret_cast<void*>(0x007AF060));
    return 1;
}
EI_REPLACE(0x005A7A70, DrawFigurePart);
