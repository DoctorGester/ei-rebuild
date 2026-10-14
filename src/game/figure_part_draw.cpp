// The draw of one figure part (005A7A70, thiscall on the part's drawer, RET 0x1C): the drawer's
// mesh (FigureMeshDrawer::mesh_, a CFigureMesh) is clip-tested against the view; animated meshes
// (type 13..16) are advanced to the current time; the mesh's colours are filled (flat meshes, flag
// 0x100) or lit once (flag 0x10 marks them lit): the lights are moved into the mesh's space through
// the inverse of the model matrix, the transform object (or the owner's figure) computes the
// colours, the "crown" mesh is pulled halfway to its average colour when 0079BD28 is set; then the
// mesh is drawn through the device (004E9010 / 004E9A50, with the per-vertex offsets added into a
// copy when there are any) or, when the mesh's owner has a figure (CObject3D::figure), through
// that figure's own slots; the screen rectangle goes to the drawer and to `rect`. See
// docs/notes/models.md ("Figure parts").
#include "ei/hook.h"
#include "figure_mesh.h"          // CFigureMesh, FigureMeshDrawer, FigureMaterial, FigureLights
#include "link_figure.h"          // CLinkMesh, ITransformPipeline
#include "map_object_figures.h"   // CObject3D
#include "model.h"                // CTransformPipeline, CSceneRenderer
#include "spell.h"                // CLight
#include "texture.h"              // CTextureDevice, CTexture
#include "world_data.h"           // SceneSun
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

// STLport string (start, finish, end of storage), handled through the original members.
struct RawString {
    char* start;
    char* finish;
    char* eos;
};
const stlp::string* AsString(const RawString* s) { return reinterpret_cast<const stlp::string*>(s); }

// The positions with the offsets added (007C2E48, a lib::AlignedArray; a function-local static,
// guard 007C2E58 bit 1, freed at exit by 005A8AE0).
lib::AlignedArray<VertexPacket>& MovedPositions() { return *reinterpret_cast<lib::AlignedArray<VertexPacket>*>(0x007C2E48); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E58); }
// The ambient level the lights' colours are set with (0079BE9C, float bits), the "crown" option
// (0079BD28), the flags an animated mesh's drawer gets (00790C0C) and the renderer (007AF060).
std::uint32_t AmbientBits() { return *reinterpret_cast<const std::uint32_t*>(0x0079BE9C); }
int CrownOption() { return *reinterpret_cast<const int*>(0x0079BD28); }
std::uint32_t AnimatedDrawerFlags() { return *reinterpret_cast<const std::uint32_t*>(0x00790C0C); }

// --- Forwarders ----------------------------------------------------------------------------------

// 004F16A0 CTransformPipeline::ClipTest (model.cpp): 2 = outside the view, 1 = crossing it.
// 005A7160: the drawer's animation at a time (float bits). 005A7290: after the draw (the renderer).
// 0068D280 (CLight::SetLitColors): a light's colours for a material and the ambient level (float bits).
__declspec(naked) void __fastcall LightForMaterial(CLight*, void*, FigureMaterial* /*material*/, std::uint32_t /*ambient*/)
    EI_ORIGINAL(0x0068D280)
// The device's mesh draws (device_mesh.cpp): DrawMesh / DrawMesh2, originals_render.h.
// STLport string members: 004217E0 string(allocator), 00425080 the end-of-storage member from an
// allocator and a value, 00428F10 assign(first, last), 0042A3D0 range initialisation (first, last,
// tag), destructors 00425040 and 0040C770 (two instantiations).
void __fastcall StringEndOfStorage(char** a0, void*, const void* a2, const void* a3) { StringEndOfStorageOriginal((void*)a0, nullptr, a2, a3); }
inline void __fastcall StringInitRange(RawString* a0, void*, const char* a2, const char* a3, const void* a4) { StringInitRangeOriginal((void*)a0, nullptr, a2, a3, a4); }
// 005197B0: map<string, LinkPartHeader>::find on a figure's part headers: the node to *out.
void __fastcall FindPiece(LinkHeaderMap* a0, void*, LinkHeaderMap::Node** a2, const RawString* a3) {
    StringMapFindOriginal(a0, nullptr, reinterpret_cast<void**>(a2), a3);
}

// The mesh's name: its slot 7, CEngineObject::GetName (the mesh is an engine object; CFigureMesh
// keeps its vtable as a plain field).
const char* MeshName(CFigureMesh* mesh) { return reinterpret_cast<CEngineObject*>(mesh)->GetName(); }

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

// The colour arrays (CFigureMesh::colorsA_ / colorsB_) are sized with the inlined SetSize that
// drops the contents (SetSizeDiscard; allocator 00406B50), the moved positions with the one that
// keeps them (SetSize; allocator 00452E70).
using ColourArray = lib::AlignedArray<std::uint32_t>;

void Fill(ColourArray& a, std::uint32_t value) {
    for (int i = 0; i < a.size; ++i) a.data[i] = value;
}

// --- The part --------------------------------------------------------------------------------------

struct Part {
    FigureMeshDrawer* node;   // this
    CTextureDevice* device;
    const FigureLights* lights;
    FigureMaterial* material;
    int alpha;
    RECT* rect;
    CTexture* textureA;
    CTexture* textureB;
    CLinkMesh* figure;        // the mesh owner's figure (CObject3D::figure), or null
    int drawFlags;            // 1 rectangle wanted, 2 crossing the view, 8 material flag 0x10

    CFigureMesh* Mesh() const { return node->mesh_; }
    ColourArray& Colours() const { return Mesh()->colorsA_; }
    ColourArray& Speculars() const { return Mesh()->colorsB_; }
    bool Textured() const { return textureA && textureB; }
};

// An allocator argument for the string members: they read (at most) a byte through it.
const void* Allocator(Part& part) { return reinterpret_cast<char*>(&part.material) + 3; }

// A pointer passed to the device's draws as a dword.
std::uint32_t Word(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// Flat meshes (flag 0x100): the figure fills the colours (its FlatColours), else every colour is
// the mesh's flat colour and every specular its flat specular (both arrays sized 4 * normals first).
void FlatColours(Part& part) {
    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        RawString s;
        StringInitWithAllocator(&s, nullptr, Allocator(part));
        StringAssignSpan(&s, nullptr, name, name + std::strlen(name));
        CFigureMesh* mesh = part.Mesh();
        part.figure->FlatColours(AsString(&s), mesh->flatColour_, mesh->flatSpecular_, &part.Colours(),
                                 &part.Speculars());
        StringBufferFree(&s);
        return;
    }
    part.Colours().SetSizeDiscard(part.Mesh()->buffers_.normalCount * 4);
    part.Speculars().SetSizeDiscard(part.Mesh()->buffers_.normalCount * 4);
    Fill(part.Colours(), part.Mesh()->flatColour_);
    Fill(part.Speculars(), part.Mesh()->flatSpecular_);
}

// The inverse model matrix of the last call, used when the matrix is singular (the original then
// reads stack slots it did not write, which usually hold the previous part's inverse).
float g_lastInverse[12];

// Lighting (once, flag 0x10): the lights and the sun (SceneSun) into the mesh's space, their
// colours for the material, then the figure (LightPart) or the transform object (LightMesh) lights
// the mesh; the crown blend; the speculars become the alpha unless the material keeps them.
void Light(Part& part) {
    CTransformPipeline* pipeline = part.device->pipeline_;
    ITransformPipeline* transform = pipeline->transform_;
    InvertModelMatrix(pipeline->Top(3)->m, g_lastInverse);
    std::uint32_t ambient = AmbientBits();
    for (int i = 0; i < part.lights->count; ++i) {
        CLight* light = part.lights->data[i];
        Vector3 moved;
        TransformPoint(g_lastInverse, &light->position_.x, &moved.x);
        std::memcpy(&light->localPosition_, &moved, sizeof(moved));
        LightForMaterial(light, nullptr, part.material, ambient);
    }
    if (CLight* sun = worlddata::SceneSun()) {
        Vector3 moved;
        TransformSun(g_lastInverse, &sun->direction_.x, &moved.x);
        std::memcpy(&sun->localDirection_, &moved, sizeof(moved));
        LightForMaterial(sun, nullptr, part.material, ambient);
        transform->AddSun(sun);
    }

    if (part.figure) {
        const char* name = MeshName(part.Mesh());
        RawString s;
        StringInitWithAllocator(&s, nullptr, Allocator(part));
        StringAssignSpan(&s, nullptr, name, name + std::strlen(name));
        part.figure->LightPart(AsString(&s), part.lights, part.material, part.alpha, &part.Colours(), &part.Speculars());
        StringRepDestroy(&s);
    } else {
        std::uint32_t vector[3];
        std::memcpy(vector, part.material->lightVector_, sizeof(vector));
        CFigureMesh* mesh = part.Mesh();
        transform->LightMesh(mesh->buffers_.vertices, mesh->buffers_.normals, part.Colours().data, mesh->buffers_.words,
                             mesh->buffers_.wordCount, part.lights->data, part.lights->count, vector);
    }

    if (CrownOption() != 0 && gamecrt::stricmp(MeshName(part.Mesh()), "crown") == 0) {
        CrownBlend(part.Colours().data, part.Colours().size);
    }
    if (!(part.material->flags_ & 0x10)) {
        Fill(part.Speculars(), (static_cast<std::uint32_t>(part.alpha) & 0xFF) << 24);
    }
    part.Mesh()->flags_ |= 0x10;
}

// The positions with the per-vertex offsets (CFigureMesh::offsets_) added, in 007C2E48.
const void* MovedPositionsOf(CFigureMesh* mesh) {
    lib::AlignedArray<VertexPacket>& moved = MovedPositions();
    moved.SetSize(mesh->buffers_.vertexCount);
    std::memcpy(moved.data, mesh->buffers_.vertices, static_cast<std::size_t>(mesh->buffers_.vertexCount) * 64);
    for (int i = 0; i < mesh->offsets_.size; ++i) {
        const float* offset = &mesh->offsets_.data[i].x;
        VertexPacket& packet = moved.data[i >> 2];
        AddOffset(&offset[0], &packet.block[0][i & 3]);
        AddOffset(&offset[1], &packet.block[1][i & 3]);
        AddOffset(&offset[2], &packet.block[2][i & 3]);
    }
    return moved.data;
}

// The mesh through the device. The drawn flag (0x40) is only set when a rectangle was asked for.
void DrawThroughDevice(Part& part) {
    CFigureMesh* mesh = part.Mesh();
    const void* positions = mesh->buffers_.vertices;
    int blocks = mesh->buffers_.vertexCount;
    if (mesh->offsets_.size != 0) positions = MovedPositionsOf(mesh);
    mesh = part.Mesh();
    const MeshBuffers& b = mesh->buffers_;
    int result;
    if (part.Textured()) {
        result = DrawMesh2(part.device, nullptr, part.textureA->surface_, part.textureB->surface_, positions, blocks,
                           Word(b.normals), b.normalCount, Word(mesh->colorsA_.data), Word(mesh->colorsB_.data),
                           Word(b.texCoords), b.texCoordCount, Word(b.corners), b.cornerCount, Word(b.words),
                           b.wordCount, Word(b.indices), b.indexCount, mesh->textureOffsetU_, mesh->textureOffsetV_, part.alpha,
                           part.drawFlags);
    } else {
        result = DrawMesh(reinterpret_cast<char*>(part.device), nullptr, positions, blocks, Word(b.normals),
                          b.normalCount, Word(mesh->colorsA_.data), Word(mesh->colorsB_.data), Word(b.texCoords),
                          b.texCoordCount, Word(b.corners), b.cornerCount, Word(b.words), b.wordCount, Word(b.indices),
                          b.indexCount, mesh->textureOffsetU_, mesh->textureOffsetV_, part.alpha, part.drawFlags);
    }
    if (!part.rect) return;
    RECT* own = &part.node->screen_;
    if (result == 2) {
        gameapi::SetRectEmpty(own);
        gameapi::SetRectEmpty(part.rect);
        return;
    }
    gameapi::CopyRect(own, &part.device->drawnRect_);
    *part.rect = *own;
    part.Mesh()->flags_ |= 0x40;
}

// The mesh through the owner's figure: DrawPartMoved(Textured) with the offsets, DrawPart(Textured)
// without; the rectangle from the figure's part header of that name.
void DrawThroughFigure(Part& part) {
    CFigureMesh* mesh = part.Mesh();
    bool moved = mesh->offsets_.size != 0;
    const char* name = MeshName(mesh);
    RawString s;
    if (moved || part.Textured()) {
        StringInitWithAllocator(&s, nullptr, Allocator(part));
        StringInitRange(&s, nullptr, name, name + std::strlen(name), part.material);
    } else {
        s.start = nullptr;
        s.finish = nullptr;
        int zero = 0;
        StringEndOfStorage(&s.eos, nullptr, Allocator(part), &zero);
        StringAssignSpan(&s, nullptr, name, name + std::strlen(name));
    }
    mesh = part.Mesh();
    std::uint32_t u = mesh->textureOffsetU_;
    std::uint32_t v = mesh->textureOffsetV_;
    ColourArray* colours = &part.Colours();
    ColourArray* speculars = &part.Speculars();
    lib::AlignedArray<Vector3>* offsets = &mesh->offsets_;
    int result;
    if (moved && part.Textured()) {
        result = part.figure->DrawPartMovedTextured(AsString(&s), part.drawFlags, part.textureA->surface_,
                                                    part.textureB->surface_, colours, speculars, offsets, u, v);
    } else if (moved) {
        result = part.figure->DrawPartMoved(AsString(&s), part.drawFlags, colours, speculars, offsets, u, v);
    } else if (part.Textured()) {
        result = part.figure->DrawPartTextured(AsString(&s), part.drawFlags, part.textureA->surface_,
                                               part.textureB->surface_, colours, speculars, u, v);
    } else {
        result = part.figure->DrawPart(AsString(&s), part.drawFlags, colours, speculars, u, v);
    }
    StringBufferFree(&s);

    if (part.rect) {
        const char* pieceName = MeshName(part.Mesh());
        RawString key;
        key.start = nullptr;
        key.finish = nullptr;
        int zero = 0;
        StringEndOfStorage(&key.eos, nullptr, Allocator(part), &zero);
        StringAssignSpan(&key, nullptr, pieceName, pieceName + std::strlen(pieceName));
        LinkHeaderMap::Node* node = nullptr;
        FindPiece(&part.figure->headers_, nullptr, &node, &key);
        RECT* own = &part.node->screen_;
        gameapi::CopyRect(own, &node->second.screen);
        StringBufferFree(&key);
        *part.rect = *own;
    }
    if (result != 2) part.Mesh()->flags_ |= 0x40;
}

// 005A8AE0: frees the moved positions at exit (registered by its game address, as the original does).
constexpr std::uintptr_t kFreeMovedPositions = std::uintptr_t(0x005A8AE0);

} // namespace

// 005A7A70 (thiscall on the part's drawer, RET 0x1C). `lights` the light objects, `material` the
// drawing context (flags, light vector), `alpha` the low byte for the speculars, `rect` (optional)
// receives the screen rectangle, the textures (their surfaces) select the two-texture draw.
// Returns 1.
int __fastcall DrawFigurePart(FigureMeshDrawer* node, void*, CTextureDevice* device, const FigureLights* lights,
                              FigureMaterial* material, int alpha, RECT* rect, CTexture* textureA, CTexture* textureB) {
    CFigureMesh* mesh = node->mesh_;
    mesh->flags_ &= ~0x40u;
    if (node->mesh_->flags_ & 0x80) return 1;
    CObject3D* owner = node->mesh_->owner_;
    Part part{node, device, lights, material, alpha, rect, textureA, textureB,
              owner ? reinterpret_cast<CLinkMesh*>(owner->figure) : nullptr, 0};
    if (!(Guard() & 1)) {
        Guard() |= 1;
        MovedPositions() = lib::AlignedArray<VertexPacket>{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(kFreeMovedPositions));
    }

    int visibility = ClipTest(device->pipeline_, nullptr, &part.Mesh()->bounds_);
    if (visibility == 2) return 1;
    int type = static_cast<int>(part.Mesh()->meshType_);
    if (type >= 13 && type <= 16) {
        float time;
        TickTime(&time);
        node->flags_ |= AnimatedDrawerFlags();
        std::uint32_t bits;
        std::memcpy(&bits, &time, 4);
        AnimateOriginal(reinterpret_cast<char*>(node), nullptr, bits);
    }

    std::uint32_t meshFlags = part.Mesh()->flags_;
    if (meshFlags & 0x100) {
        FlatColours(part);
    } else if (!(meshFlags & 0x10)) {
        Light(part);
    }

    if (visibility == 1) part.drawFlags = 2;
    if (rect) part.drawFlags |= 1;
    if (material->flags_ & 0x10) part.drawFlags |= 8;
    if (part.figure) {
        DrawThroughFigure(part);
    } else {
        DrawThroughDevice(part);
    }
    FinishDraw(reinterpret_cast<char*>(node), nullptr, SceneContext());
    return 1;
}
EI_REPLACE(0x005A7A70, DrawFigurePart);
