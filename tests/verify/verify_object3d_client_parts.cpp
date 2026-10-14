// Checks for object3d_client_parts.cpp and object3d_halves.cpp (A-U12).
// Side by side (pure; the original and ours both run, results compared):
// - FindMaterial (0051B5B0): every part added to a client object (zone load, equipment).
// - ShadowsShown (0051E200): the client's shadow refresh (world_client_maps.cpp) and DrawShadow.
// - ShadowInView (0051E300): CollectVisible each frame for objects off screen; the bounds' centre
//   is put back before ours runs and compared after both.
// - MatrixSetIdentity (0051E1C0): the original into a scratch matrix. No scenario reaches it (its
//   only caller, DrawShadow, is rewritten and sets its matrix itself).
// - The server half's class name / id (0051F2C0 / 0051F2D0) and Create (0051F320).
// Ours only (they change the object; the state afterwards is checked):
// - The client half's lifetime (0051AB00, 0051AEC0, 0051AF70, 0051AD90, 0051AD70): every client
//   object made and dropped (zone load / unload, spawns, deaths).
// - PartAdded / FindOrAddTexture / FindOrAddMaterial (0051B050, 0051B2F0, 0051B470): the figures of
//   every client object built; PartRemoved / RemovePartNamed (0051B160, 0051B210): equipment taken
//   off a unit, a figure rebuilt.
// - ReplaceTexture / SetSkins / ApplyMaterials (0051B630, 0051C250, 0051CB30): after every
//   figure load; hero skins.
// - SetLit / its slot (0051C9F0, 0051C9E0): an object highlighted (the cursor over it) and back.
// - ClearShadow (0051E280): shadows switched off for a kind in the settings.
// - PopMatrix (0051C110): every object drawn.
// - TextureParts::Destruct / PartGroup::ReleaseParts (0051A370, 0051AAA0): DestroyClient.
// - The server half (0051F240, 0051F300, 0051F2E0, 0051F330) and its empty slots: every server
//   object of a hosted game. The base half's empty slots (0051AC60-0051ACD0): the server half's
//   draw and picking slots; no scenario is known to call them.
// - The wounds material's destructors (0051C730, 0051C710): a wounded unit healed or dropped.
// - NamedTextureDestruct / NamedTextureCopy (0051C800, 0051E430): the ground prints' lists (scorch
//   marks, footprints) growing and emptied. MaterialStageCopy (0051E970): no scenario (its callers
//   in ShowWounds are rewritten).
#include "verify.h"

#include "../../src/game/ground_print.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"

#include <cstring>

void __fastcall MatrixSetIdentity(Matrix4* matrix);
int __fastcall SpecificBaseDraw(CObject3DSpecific*, void*, void*, int*);
int __fastcall SpecificBaseZero(CObject3DSpecific*, void*, void*);
void __fastcall SpecificBaseNothing(CObject3DSpecific*);
void __fastcall SpecificBaseIgnore(CObject3DSpecific*, void*, int);
CObject3DSpecific* __fastcall Object3DServerConstruct(CObject3DSpecific* self);
void __fastcall Object3DServerDestruct(CObject3DSpecific* self);
CObject3DSpecific* __fastcall Object3DServerDelete(CObject3DSpecific* self, void*, unsigned flags);
int __fastcall Object3DServerCreate(CObject3DSpecific*);
void __fastcall Object3DServerDestroy(CObject3DSpecific* self);
const char* __fastcall Object3DServerClassName(CObject3DSpecific*);
int __fastcall Object3DServerClassId(CObject3DSpecific*);
void __fastcall WoundsMaterialDestruct(WoundsMaterial* self);
WoundsMaterial* __fastcall WoundsMaterialDelete(WoundsMaterial* self, void*, unsigned flags);
void __fastcall NamedTextureDestruct(NamedTexture* self);
NamedTexture* __fastcall NamedTextureCopy(NamedTexture* self, void*, const NamedTexture* other);
void __fastcall MaterialStageCopy(MaterialStage* place, const MaterialStage* stage);

namespace {
void* s_unused;

constexpr std::uintptr_t kClientVtable = std::uintptr_t(0x0073DE90);
constexpr std::uintptr_t kSpecificBaseVtable = std::uintptr_t(0x0073DEFC);
constexpr std::uintptr_t kServerVtable = std::uintptr_t(0x0073DFBC);

std::uintptr_t VtableOf(const void* object) { return *static_cast<const std::uintptr_t*>(object); }

// Whether a group of either part set holds `part`.
bool Holds(const CObject3DClientSpecific* self, const CFigureMesh* part) {
    for (const TextureParts* set : self->partSets) {
        if (!set) continue;
        for (int g = 0; g < set->groups.size; ++g)
            for (int i = 0; i < set->groups[g]->parts.size; ++i)
                if (set->groups[g]->parts[i] == part) return true;
    }
    return false;
}
} // namespace

// --- Part sets and lifetime (ours only) -------------------------------------------------------------

static int __fastcall ReleasePartsVerify(PartGroup* self) {
    int result = self->ReleaseParts();
    ei::verify::Check("PartGroup::ReleaseParts", true,
                      result == 1 && !self->parts.data && self->parts.size == 0 && self->parts.capacity == 0, "");
    return result;
}
EI_VERIFY(0x0051AAA0, ReleasePartsVerify, 0, s_unused);

static void __fastcall PartsDestructVerify(TextureParts* self) {
    self->Destruct();
    ei::verify::Check("TextureParts::Destruct", true, !self->texture && self->groups.size == 0, "");
}
EI_VERIFY(0x0051A370, PartsDestructVerify, 0, s_unused);

static CObject3DClientSpecific* __fastcall ConstructVerify(CObject3DClientSpecific* self) {
    CObject3DClientSpecific* result = self->Construct();
    bool ok = result == self && VtableOf(self) == kClientVtable && !self->object && self->materialCursor == -1 &&
              self->textures.size == 0 && self->materials.size == 0 && !self->texture && !self->woundsTexture &&
              !self->woundsMaterial && self->skinName.GetLength() == 0 && self->secondSkinName.GetLength() == 0;
    ei::verify::Check("CObject3DClientSpecific::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x0051AB00, ConstructVerify, 0, s_unused);

static int __fastcall CreateClientVerify(CObject3DClientSpecific* self) {
    int result = self->CreateClient();
    bool ok = result == 1 && self->flags_ == 0 && !self->partSets[0] && !self->partSets[1] &&
              self->materialCursor == -1 && self->figureScale.x == 1.0f && self->figureScale.z == 1.0f;
    ei::verify::Check("CObject3DClientSpecific::CreateClient", true, ok, "");
    return result;
}
EI_VERIFY(0x0051AEC0, CreateClientVerify, 0, s_unused);

static void __fastcall DestroyClientVerify(CObject3DClientSpecific* self) {
    self->DestroyClient();
    bool ok = !self->partSets[0] && !self->partSets[1] && self->textures.size == 0 && self->materials.size == 0 &&
              !self->object && !self->texture && !self->woundsTexture && !self->woundsMaterial;
    ei::verify::Check("CObject3DClientSpecific::DestroyClient", true, ok, "");
}
EI_VERIFY(0x0051AF70, DestroyClientVerify, 0, s_unused);

static void __fastcall DestructBodyVerify(CObject3DClientSpecific* self) {
    self->DestructBody();
    ei::verify::Check("CObject3DClientSpecific::DestructBody", true,
                      VtableOf(self) == CEngineObject::kVtableAddress && !self->object, "");
}
EI_VERIFY(0x0051AD90, DestructBodyVerify, 0, s_unused);

static CObject3DClientSpecific* __fastcall DeleteClientVerify(CObject3DClientSpecific* self, void*, unsigned flags) {
    CObject3DClientSpecific* result = self->DeleteClient(flags);
    ei::verify::Check("CObject3DClientSpecific::DeleteClient", static_cast<void*>(self), static_cast<void*>(result),
                      "flags %u", flags);
    return result;
}
EI_VERIFY(0x0051AD70, DeleteClientVerify, 0, s_unused);

// --- Parts, textures and materials --------------------------------------------------------------------

// 0051B5B0: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV [ESP+0xC],ECX (8 bytes).
static CMaterial*(__fastcall* s_findMaterial)(CObject3DClientSpecific*, void*, int, int);
static CMaterial* __fastcall FindMaterialVerify(CObject3DClientSpecific* self, void*, int texture, int material) {
    CMaterial* expected = s_findMaterial(self, nullptr, texture, material);
    CMaterial* actual = self->FindMaterial(texture, material);
    ei::verify::Check("CObject3DClientSpecific::FindMaterial", static_cast<void*>(expected), static_cast<void*>(actual),
                      "texture %d material %d", texture, material);
    return expected;
}
EI_VERIFY(0x0051B5B0, FindMaterialVerify, 8, s_findMaterial);

static CTexture* __fastcall FindOrAddTextureVerify(CObject3DClientSpecific* self, void*, int kind) {
    CTexture* result = self->FindOrAddTexture(kind);
    bool listed = false;
    for (int i = 0; i < self->textures.size; ++i) listed = listed || self->textures[i] == result;
    ei::verify::Check("CObject3DClientSpecific::FindOrAddTexture", true, result && result->type_ == kind && listed,
                      "kind %d", kind);
    return result;
}
EI_VERIFY(0x0051B2F0, FindOrAddTextureVerify, 0, s_unused);

static CMaterial* __fastcall FindOrAddMaterialVerify(CObject3DClientSpecific* self, void*, int texture, int material) {
    CMaterial* result = self->FindOrAddMaterial(texture, material);
    bool ok = !result || (result->type_ == material && self->FindMaterial(texture, material) == result);
    ei::verify::Check("CObject3DClientSpecific::FindOrAddMaterial", true, ok, "texture %d material %d", texture,
                      material);
    return result;
}
EI_VERIFY(0x0051B470, FindOrAddMaterialVerify, 0, s_unused);

static int __fastcall PartAddedVerify(CObject3DClientSpecific* self, void*, CFigureMesh* part) {
    int result = self->PartAdded(part);
    ei::verify::Check("CObject3DClientSpecific::PartAdded", true, result == 0 || Holds(self, part), "result %d", result);
    return result;
}
EI_VERIFY(0x0051B050, PartAddedVerify, 0, s_unused);

static int __fastcall PartRemovedVerify(CObject3DClientSpecific* self, void*, CFigureMesh* part) {
    bool held = Holds(self, part);
    int result = self->PartRemoved(part);
    ei::verify::Check("CObject3DClientSpecific::PartRemoved", held ? 1 : 0, result, "");
    return result;
}
EI_VERIFY(0x0051B160, PartRemovedVerify, 0, s_unused);

static CFigureMesh* __fastcall RemovePartNamedVerify(CObject3DClientSpecific* self, void*, const char* name) {
    CFigureMesh* named = self->AsSpecific()->FindPart(name);
    bool held = named && Holds(self, named);
    CFigureMesh* result = self->RemovePartNamed(name);
    ei::verify::Check("CObject3DClientSpecific::RemovePartNamed", static_cast<void*>(held ? named : nullptr),
                      static_cast<void*>(result), "%s", name);
    return result;
}
EI_VERIFY(0x0051B210, RemovePartNamedVerify, 0, s_unused);

static CTexture* __fastcall ReplaceTextureVerify(CObject3DClientSpecific* self, void*, CTexture* texture) {
    CTexture* result = self->ReplaceTexture(texture);
    bool listed = false;
    for (int i = 0; i < self->textures.size; ++i) listed = listed || self->textures[i] == texture;
    ei::verify::Check("CObject3DClientSpecific::ReplaceTexture", true, !result || listed, "");
    return result;
}
EI_VERIFY(0x0051B630, ReplaceTextureVerify, 0, s_unused);

static int __fastcall SetSkinsVerify(CObject3DClientSpecific* self, void*, const char* first, const char* second) {
    int result = self->SetSkins(first, second);
    bool ok = result == 1 && std::strcmp(self->skinName, first ? first : "") == 0 &&
              std::strcmp(self->secondSkinName, second ? second : "") == 0;
    ei::verify::Check("CObject3DClientSpecific::SetSkins", true, ok, "%s / %s", first ? first : "(null)",
                      second ? second : "(null)");
    return result;
}
EI_VERIFY(0x0051C250, SetSkinsVerify, 0, s_unused);

static void __fastcall ApplyMaterialsVerify(CObject3DClientSpecific* self) {
    self->ApplyMaterials();
    bool ok = self->materials.size <= 0 ? self->materialCursor == -1
                                        : self->materialCursor >= 0 && self->materialCursor <= self->materials.size;
    ei::verify::Check("CObject3DClientSpecific::ApplyMaterials", true, ok, "cursor %d of %d", self->materialCursor,
                      self->materials.size);
}
EI_VERIFY(0x0051CB30, ApplyMaterialsVerify, 0, s_unused);

namespace {
// The total of the materials' kept emissive colours.
std::size_t KeptColours(const CObject3DClientSpecific* self) {
    std::size_t total = 0;
    for (int i = 0; i < self->materials.size; ++i) total += self->materials[i]->entries.size();
    return total;
}
bool AllWhite(const CObject3DClientSpecific* self) {
    for (int i = 0; i < self->materials.size; ++i) {
        const Vector4& e = self->materials[i]->colors.emissive;
        if (e.x != 1.0f || e.y != 1.0f || e.z != 1.0f) return false;
    }
    return true;
}
void CheckLit(const char* name, const CObject3DClientSpecific* self, int lit, std::size_t before) {
    std::size_t after = KeptColours(self);
    bool ok = lit ? after == before + static_cast<std::size_t>(self->materials.size) && AllWhite(self) : after <= before;
    ei::verify::Check(name, true, ok, "lit %d: %u -> %u kept", lit, static_cast<unsigned>(before),
                      static_cast<unsigned>(after));
}
} // namespace

static void __fastcall SetLitVerify(CObject3DClientSpecific* self, void*, int lit) {
    std::size_t before = KeptColours(self);
    self->SetLit(lit);
    CheckLit("CObject3DClientSpecific::SetLit", self, lit, before);
}
EI_VERIFY(0x0051C9F0, SetLitVerify, 0, s_unused);

static void __fastcall SetLitSlotVerify(CObject3DClientSpecific* self, void*, int lit) {
    std::size_t before = KeptColours(self);
    self->SetLitSlot(lit);
    CheckLit("CObject3DClientSpecific::SetLitSlot", self, lit, before);
}
EI_VERIFY(0x0051C9E0, SetLitSlotVerify, 0, s_unused);

static void __fastcall EmptySlot25Verify(CObject3DClientSpecific* self, void*, int value) {
    self->EmptySlot25(value);
    ei::verify::Check("CObject3DClientSpecific::EmptySlot25", true, true, "");
}
EI_VERIFY(0x0051C9D0, EmptySlot25Verify, 0, s_unused);

// --- Shadow state ---------------------------------------------------------------------------------------

// 0051E200: MOV EAX,[0079BD0C] (5 bytes).
static std::uint32_t(__fastcall* s_shadowsShown)(CObject3DClientSpecific*);
static std::uint32_t __fastcall ShadowsShownVerify(CObject3DClientSpecific* self) {
    std::uint32_t expected = s_shadowsShown(self);
    std::uint32_t actual = self->ShadowsShown();
    ei::verify::Check("CObject3DClientSpecific::ShadowsShown", expected, actual, "");
    return expected;
}
EI_VERIFY(0x0051E200, ShadowsShownVerify, 5, s_shadowsShown);

static void __fastcall ClearShadowVerify(CObject3DClientSpecific* self) {
    self->ClearShadow();
    bool ok = self->vertices.size == 0 && self->texCoords.size == 0 && !self->indices.data && self->indices.size == 0 &&
              self->indices.capacity == 0 && !self->texture && self->sphere.flags == 0;
    ei::verify::Check("CObject3DClientSpecific::ClearShadow", true, ok, "");
}
EI_VERIFY(0x0051E280, ClearShadowVerify, 0, s_unused);

// 0051E300: MOV EAX,[0079BD0C] (5 bytes).
static bool(__fastcall* s_shadowInView)(CObject3DClientSpecific*);
static bool __fastcall ShadowInViewVerify(CObject3DClientSpecific* self) {
    const Vector3 before = self->sphere.center;
    bool expected = s_shadowInView(self);
    const Vector3 after = self->sphere.center;
    self->sphere.center = before;
    bool actual = self->ShadowInView();
    bool same = expected == actual && std::memcmp(&after, &self->sphere.center, sizeof(Vector3)) == 0;
    ei::verify::Check("CObject3DClientSpecific::ShadowInView", true, same, "%d / %d", expected, actual);
    return expected;
}
EI_VERIFY(0x0051E300, ShadowInViewVerify, 5, s_shadowInView);

static void __fastcall PopMatrixVerify(CTransformPipeline* self, void*, int index) {
    int before = self->stacks_[index].count;
    self->PopMatrix(index);
    ei::verify::Check("CTransformPipeline::PopMatrix", before != 0 ? before - 1 : 0, self->stacks_[index].count,
                      "stack %d", index);
}
EI_VERIFY(0x0051C110, PopMatrixVerify, 0, s_unused);

// 0051E1C0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static void(__fastcall* s_setIdentity)(Matrix4*);
static void __fastcall SetIdentityVerify(Matrix4* matrix) {
    Matrix4 expected;
    s_setIdentity(&expected);
    MatrixSetIdentity(matrix);
    ei::verify::Check("MatrixSetIdentity", 0, std::memcmp(&expected, matrix, sizeof(Matrix4)), "");
}
EI_VERIFY(0x0051E1C0, SetIdentityVerify, 6, s_setIdentity);

// --- The base half's empty slots and the server half ----------------------------------------------------

static int __fastcall BaseDrawVerify(CObject3DSpecific* self, void*, void* context, int* dirty) {
    int result = SpecificBaseDraw(self, nullptr, context, dirty);
    ei::verify::Check("CObject3DSpecific::Draw", 0, result, "");
    return result;
}
EI_VERIFY(0x0051AC60, BaseDrawVerify, 0, s_unused);

static int __fastcall BaseZeroVerify(CObject3DSpecific* self, void*, void* value) {
    int result = SpecificBaseZero(self, nullptr, value);
    ei::verify::Check("CObject3DSpecific empty slot", 0, result, "");
    return result;
}
EI_VERIFY(0x0051AC70, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051AC90, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051ACB0, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051ACA0, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051F270, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051F290, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051F280, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051F2B0, BaseZeroVerify, 0, s_unused);
EI_VERIFY(0x0051F2A0, BaseZeroVerify, 0, s_unused);

static void __fastcall BaseIgnoreVerify(CObject3DSpecific* self, void*, int value) {
    SpecificBaseIgnore(self, nullptr, value);
    ei::verify::Check("CObject3DSpecific ignored slot", true, true, "");
}
EI_VERIFY(0x0051ACC0, BaseIgnoreVerify, 0, s_unused);
EI_VERIFY(0x0051ACD0, BaseIgnoreVerify, 0, s_unused);

static CObject3DSpecific* __fastcall ServerConstructVerify(CObject3DSpecific* self) {
    CObject3DSpecific* result = Object3DServerConstruct(self);
    bool ok = result == self && VtableOf(self) == kServerVtable && !self->object && self->refCount_ == 0 &&
              self->id_ == -1 && self->flags_ == 0 && self->type_ == 0 && !self->owner_ && self->prototypeId_ == 0;
    ei::verify::Check("CObject3DServer::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x0051F240, ServerConstructVerify, 0, s_unused);

static void __fastcall ServerDestructVerify(CObject3DSpecific* self) {
    Object3DServerDestruct(self);
    ei::verify::Check("CObject3DServer::Destruct", true,
                      VtableOf(self) == CEngineObject::kVtableAddress && !self->object, "");
}
EI_VERIFY(0x0051F300, ServerDestructVerify, 0, s_unused);

static CObject3DSpecific* __fastcall ServerDeleteVerify(CObject3DSpecific* self, void*, unsigned flags) {
    CObject3DSpecific* result = Object3DServerDelete(self, nullptr, flags);
    ei::verify::Check("CObject3DServer::Delete", static_cast<void*>(self), static_cast<void*>(result), "flags %u", flags);
    return result;
}
EI_VERIFY(0x0051F2E0, ServerDeleteVerify, 0, s_unused);

// 0051F320: MOV EAX,1 (5 bytes).
static int(__fastcall* s_serverCreate)(CObject3DSpecific*);
static int __fastcall ServerCreateVerify(CObject3DSpecific* self) {
    int expected = s_serverCreate(self);
    ei::verify::Check("CObject3DServer::Create", expected, Object3DServerCreate(self), "");
    return expected;
}
EI_VERIFY(0x0051F320, ServerCreateVerify, 5, s_serverCreate);

static void __fastcall ServerDestroyVerify(CObject3DSpecific* self) {
    Object3DServerDestroy(self);
    ei::verify::Check("CObject3DServer::Destroy", true, !self->object, "");
}
EI_VERIFY(0x0051F330, ServerDestroyVerify, 0, s_unused);

// 0051F2C0 / 0051F2D0: MOV EAX,imm32 (5 bytes).
static const char*(__fastcall* s_serverName)(CObject3DSpecific*);
static const char* __fastcall ServerNameVerify(CObject3DSpecific* self) {
    const char* expected = s_serverName(self);
    const char* actual = Object3DServerClassName(self);
    ei::verify::Check("CObject3DServer::GetClassName", true,
                      expected == actual && std::strcmp(actual, "CObject3DServer") == 0, "%s", expected);
    return expected;
}
EI_VERIFY(0x0051F2C0, ServerNameVerify, 5, s_serverName);

static int(__fastcall* s_serverId)(CObject3DSpecific*);
static int __fastcall ServerIdVerify(CObject3DSpecific* self) {
    int expected = s_serverId(self);
    ei::verify::Check("CObject3DServer::GetClassId", expected, Object3DServerClassId(self), "");
    return expected;
}
EI_VERIFY(0x0051F2D0, ServerIdVerify, 5, s_serverId);

// --- The wounds material and the stage copies -----------------------------------------------------------

static void __fastcall WoundsDestructVerify(WoundsMaterial* self) {
    WoundsMaterialDestruct(self);
    ei::verify::Check("WoundsMaterial::Destruct", WoundsMaterial::kRootVtableAddress, VtableOf(self), "");
}
EI_VERIFY(0x0051C730, WoundsDestructVerify, 0, s_unused);

static WoundsMaterial* __fastcall WoundsDeleteVerify(WoundsMaterial* self, void*, unsigned flags) {
    WoundsMaterial* result = WoundsMaterialDelete(self, nullptr, flags);
    ei::verify::Check("WoundsMaterial::Delete", static_cast<void*>(self), static_cast<void*>(result), "flags %u", flags);
    return result;
}
EI_VERIFY(0x0051C710, WoundsDeleteVerify, 0, s_unused);

static void __fastcall NamedTextureDestructVerify(NamedTexture* self) {
    CTexture* texture = self->texture;
    int before = texture ? texture->refCount_ : 0;
    NamedTextureDestruct(self);
    // The texture is looked at again only when another holder keeps it alive.
    bool ok = before < 2 || texture->refCount_ == before - 1;
    ei::verify::Check("NamedTexture::Destruct", true, ok, "%d references before", before);
}
EI_VERIFY(0x0051C800, NamedTextureDestructVerify, 0, s_unused);

static NamedTexture* __fastcall NamedTextureCopyVerify(NamedTexture* self, void*, const NamedTexture* other) {
    int before = other->texture ? other->texture->refCount_ : 0;
    NamedTexture* result = NamedTextureCopy(self, nullptr, other);
    bool ok = result == self && self->name == other->name && self->name.data() != other->name.data() &&
              self->texture == other->texture && (!self->texture || self->texture->refCount_ == before + 1);
    ei::verify::Check("NamedTexture::Copy", true, ok, "");
    return result;
}
EI_VERIFY(0x0051E430, NamedTextureCopyVerify, 0, s_unused);

static void __fastcall MaterialStageCopyVerify(MaterialStage* place, const MaterialStage* stage) {
    MaterialStageCopy(place, stage);
    bool ok = !place || (place->texCoordIndex == stage->texCoordIndex && place->name == stage->name &&
                         place->texture == stage->texture);
    ei::verify::Check("MaterialStage::Copy", true, ok, "");
}
EI_VERIFY(0x0051E970, MaterialStageCopyVerify, 0, s_unused);
