// A party unit's wounds, face texture and weapon point on the client (CUnitClientSpecific): the wound level of each body part
// (00536EA0) and the composed wounds texture the figure shows (005370F0, called by RefreshWounds
// 00537360). See unit_client_models.h.
#include "unit_client_models.h"

#include "core_maths.h"
#include "item.h"
#include "item_db.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "model.h"     // CSceneRenderer, CRenderHelper
#include "texture.h"   // CTexture, CTextureManager
#include "ui.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_units.h"

#include <cstring>
#include <new>

// windows.h (through texture.h) renames CEngineObject's GetClassName.
#undef GetClassName

namespace {
constexpr int kBodyParts = 6;
constexpr int kTypeHero = 0x32;
constexpr float kFull = 1.0f;          // 0073B84C
constexpr float kMedium = 0.33f;       // 0073D5A4
constexpr float kLight = 0.75f;        // 0073C524
constexpr int kLegsLeft = 4, kLegsRight = 5, kHandLeft = 2, kHandRight = 3, kBody = 1, kHead = 0;
// armourIds: 0 the helmet, 1 the body armour, 2 the trousers.
constexpr int kHelmetSlot = 0, kBodyArmourSlot = 1, kTrousersSlot = 2;
constexpr std::uint32_t kNoArmour = 0xFFFFFFFF;

constexpr int kHeroTextureSize = 0x80;
constexpr int kTextureSize = 0x40;
constexpr int kMipCount = 3;
constexpr int kFormat = 0x4444;
constexpr const char* kWoundTexture = "%s%sw%d.mmp";   // 0078FB8C
constexpr const char* kWoundCacheName = "mo%dw";       // 0078FB84
constexpr const char* kHeroParts[kBodyParts] = {"hd", "bd", "lh", "rh", "ll", "rl"};   // 0078F974
constexpr const char* kParts[kBodyParts] = {"hd", "bd", "h", "h", "l", "l"};           // 0078F990

// The level of one part (0 none .. 3 heavy).
std::uint8_t PartLevel(const UnitBodyPart& part) {
    if (part.state == 0 || part.maxDurability == 0) return 0;
    float maximum = static_cast<float>(part.maxDurability);
    float ratio = static_cast<float>(part.durability) / maximum;
    if (!(ratio < kFull || ratio > kFull)) return 0;   // equal (or NaN): unhurt
    float stored = StoreFloat(ratio);
    if (!(stored > kFull / maximum)) return 3;
    if (!(stored > kMedium)) return 2;
    if (!(stored < kLight)) return 0;
    return 1;
}

// Whether the armour piece `id` (packed: prototype index in the high half) shows the wounds
// under it. The original reads the record unchecked (an out-of-range index reads address 0xB4).
bool ShowsWounds(std::uint32_t id) {
    const auto* record = reinterpret_cast<const itemdb::ArmorRecord*>(itemdb::Armors().At(id >> 16, itemdb::kArmorSize));
    return record->showsWounds != 0;
}

std::uint8_t Worse(std::uint8_t a, std::uint8_t b) { return a > b ? a : b; }
} // namespace

bool CUnitClientSpecific::WoundLevels(std::uint8_t** levels) {
    bool wounded = false;
    for (int i = 0; i < kBodyParts; ++i) {
        std::uint8_t level = PartLevel(unit->bodyParts[i]);
        (*levels)[i] = level;
        if (level) wounded = true;
    }
    std::uint8_t* out = *levels;
    if (unit->type_ == kTypeHero) {
        if (armourIds[kTrousersSlot] != kNoArmour && !ShowsWounds(armourIds[kTrousersSlot])) {
            out[kLegsRight] = 0;
            out[kLegsLeft] = 0;
        }
        if (armourIds[kBodyArmourSlot] != kNoArmour && !ShowsWounds(armourIds[kBodyArmourSlot])) {
            out[kHandRight] = 0;
            out[kHandLeft] = 0;
            out[kBody] = 0;
        }
        if (armourIds[kHelmetSlot] != kNoArmour && !ShowsWounds(armourIds[kHelmetSlot])) out[kHead] = 0;
        return wounded;
    }
    out[kLegsLeft] = Worse(out[kLegsLeft], out[kLegsRight]);
    out[kLegsRight] = out[kLegsLeft];
    out[kHandLeft] = Worse(out[kHandLeft], out[kHandRight]);
    out[kHandRight] = out[kHandLeft];
    return wounded;
}
EI_REPLACE_METHOD(0x00536EA0, CUnitClientSpecific::WoundLevels);

int CUnitClientSpecific::ComposeWoundsTexture() {
    int result = 0;
    {
        mfc::CString name;
        mfc::CString out;
        // The request lives in raw storage so that its parts go in the original's order.
        alignas(SkinRequest) unsigned char storage[sizeof(SkinRequest)];
        auto* request = new (storage) SkinRequest{nullptr, 0, 0, 0, {}, 0};
        // 004ECB50: six level bytes in 32-byte aligned storage (the block kept before them).
        auto* block = static_cast<char*>(gamecrt::operator_new(kBodyParts + 0x24));
        request->layers = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
        reinterpret_cast<void**>(request->layers)[-1] = block;
        request->capacity = kBodyParts;
        request->layerBytes = kBodyParts;
        if (WoundLevels(&request->layers)) {
            bool hero = unit->type_ == kTypeHero;
            request->flags = hero ? kHeroTextureSize : kTextureSize;
            for (int i = 0; i < kBodyParts; ++i) {
                std::uint8_t level = request->layers[i];
                if (!level) continue;
                name.Format(kWoundTexture, unit->prototypeName, unit->type_ == kTypeHero ? kHeroParts[i] : kParts[i],
                            static_cast<int>(level));
                request->textures.SetAtGrow(request->textures.GetSize(), name);
            }
            name.Format(kWoundCacheName, unit->id_);
            result = TextureManagerComposeOriginal(UiTextureManager(), nullptr, &out, request, &name, &name, kMipCount, 0,
                                                   kFormat);
        }
        request->textures.~CStringArray();
        gamecrt::operator_delete(reinterpret_cast<void**>(request->layers)[-1]);
    }
    return result;
}
EI_REPLACE_METHOD(0x005370F0, CUnitClientSpecific::ComposeWoundsTexture);

void CObject3DClientSpecific::HideWounds() {
    CEngineObject* oldTexture = woundsTexture;
    woundsTexture = nullptr;
    if (oldTexture) oldTexture->Release();
    WoundsMaterial* oldMaterial = woundsMaterial;
    woundsMaterial = nullptr;
    if (oldMaterial && --oldMaterial->refCount == 0) oldMaterial->DeletingDestructor(1);
}
EI_REPLACE_METHOD(0x0051C920, CObject3DClientSpecific::HideWounds);

namespace {
constexpr int kWoundsTextureKind = 6;
constexpr unsigned kWoundsPreset = 4;
constexpr int kSourceAlpha = 5;          // D3DBLEND_SRCALPHA
constexpr int kInverseSourceAlpha = 6;   // D3DBLEND_INVSRCALPHA
constexpr const char* kNoWoundsTexture = "Can't create wounds texture for object \"%s\" ID = %d\n";   // 0078F54C

// 004EBE60: *slot = texture with counted references; the old one released (its slot 4).
void AssignTexture(CTexture** slot, CTexture* value) {
    CTexture* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old) old->Release();
}

// 0051E570 / 0051E4F0: a counted root (refs at +4, slot 0 deletes) assigned; the old one deleted at
// its last reference.
int& RefsOf(WoundsMaterial* material) { return material->refCount; }
int& RefsOf(CRenderPreset* preset) { return preset->refs_; }
void DeleteRoot(WoundsMaterial* material) { material->DeletingDestructor(1); }
void DeleteRoot(CRenderPreset* preset) { preset->DeleteThis(1); }
template <typename T>
void AssignCountedRoot(T** slot, T* value) {
    T* old = *slot;
    *slot = value;
    if (value) ++RefsOf(value);
    if (old && --RefsOf(old) == 0) DeleteRoot(old);
}

// The material's constructor, inlined by 0051C3D0: the counted root's fields, then its own.
WoundsMaterial* NewWoundsMaterial() {
    auto* material = static_cast<WoundsMaterial*>(gamecrt::operator_new(sizeof(WoundsMaterial)));
    if (!material) return nullptr;
    material->refCount = 0;
    ei::SetVtable(material, WoundsMaterial::kRootVtableAddress);
    material->blend = 1;
    material->sourceBlend = kSourceAlpha;
    material->destBlend = kInverseSourceAlpha;
    material->preset = nullptr;
    new (&material->stages) stlp::vector<MaterialStage>();
    ei::SetVtable(material, WoundsMaterial::kVtableAddress);
    return material;
}
} // namespace

int CObject3DClientSpecific::ShowWounds(const char* name) {
    if (!partSets[0]) return 0;
    void* manager = UiTextureManager();
    if (!woundsTexture) {
        AssignTexture(&woundsTexture, static_cast<CTexture*>(TextureFindOriginal(manager, nullptr, kWoundsTextureKind, name)));
        if (!woundsTexture) {
            AssignTexture(&woundsTexture, static_cast<CTexture*>(TextureNamedOriginal(manager, nullptr, kWoundsTextureKind, name)));
            if (!woundsTexture) return game::FatalError(kNoWoundsTexture, object->GetClassName(), object->id_);
        }
    }
    AlignedBuffer extra{};
    static_cast<CTextureManager*>(manager)->LoadMmpExtra(name, &extra);
    const AlignedBuffer& loaded = woundsTexture->extra_;
    bool same = extra.size != 0 && loaded.size == extra.size &&
                std::memcmp(loaded.data, extra.data, static_cast<std::size_t>(extra.size)) == 0;
    if (!same) {
        woundsTexture->flags_ &= ~kTextureLoaded;
        if (!TextureLoadOriginal(manager, nullptr, woundsTexture)) {
            AssignTexture(&woundsTexture, nullptr);
            AssignCountedRoot(&woundsMaterial, static_cast<WoundsMaterial*>(nullptr));
            extra.FreeData();
            return 0;
        }
    }
    if (!woundsMaterial) {
        AssignCountedRoot(&woundsMaterial, NewWoundsMaterial());
        CRenderHelper* helper = static_cast<CSceneRenderer*>(SceneContext())->helper_;
        AssignCountedRoot(&woundsMaterial->preset, helper->Preset(kWoundsPreset));
        woundsMaterial->stages.push_back(MaterialStage(partSets[0]->texture));
        woundsMaterial->stages.push_back(MaterialStage(woundsTexture));
    }
    extra.FreeData();
    return 1;
}
EI_REPLACE_METHOD(0x0051C3D0, CObject3DClientSpecific::ShowWounds);

// --- The face texture -------------------------------------------------------------------------

namespace {
constexpr int kFaceTextureKind = 0x3D;
constexpr const char* kHeroFace = "%smouth_%.2d";      // 0078FC70
constexpr const char* kFace = "%smouth";               // 0078FC68
} // namespace

bool CUnitClientSpecific::LoadFaceTexture() {
    if (faceTexture) return true;
    const auto* prototype = reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
    if (!prototype) return false;
    char name[0x400];
    if (unit->type_ == kTypeHero) gamecrt::sprintf(name, kHeroFace, unit->prototypeName, prototype->skin);
    else gamecrt::sprintf(name, kFace, unit->prototypeName);
    auto* texture = static_cast<CEngineObject*>(TextureNamedOriginal(UiTextureManager(), nullptr, kFaceTextureKind, name));
    CEngineObject* old = faceTexture;
    faceTexture = texture;
    if (texture) ++texture->refCount_;
    if (old) old->Release();
    TextureLoadOriginal(UiTextureManager(), nullptr, faceTexture);
    CEngineObject* face = faceTexture;
    if (face->flags_ & kTextureLoaded) return true;
    faceTexture = nullptr;
    if (face) face->Release();
    blinks = 0;
    return false;
}
EI_REPLACE_METHOD(0x00538DB0, CUnitClientSpecific::LoadFaceTexture);

// --- The weapon point -------------------------------------------------------------------------

namespace {
constexpr std::uint32_t kWeaponSlots = 4;
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFF;
constexpr const char* kHandNode = "baserh3";   // 0078FB04
} // namespace

bool CUnitClientSpecific::WeaponPoint(Vector3* out) {
    mfc::CString node;
    std::uint32_t slot = unit->activeWeapon;
    std::uint32_t weapon = slot < kWeaponSlots ? weaponIds[slot] : kNoWeapon;
    if (!WeaponBaseFigure(weapon, &node)) node = kHandNode;
    const auto* found = static_cast<const std::uint8_t*>(
        AnimationStateFindNodeOriginal(unit->Object3D()->animation, nullptr, static_cast<const char*>(node)));
    if (!found) return false;
    // The bone's position in figure space (FigureBone::position, +0x74).
    const Vector3& position = reinterpret_cast<const FigureBone*>(found)->position;
    out->x = position.x;
    out->y = position.y;
    out->z = position.z;
    return true;
}
EI_REPLACE_METHOD(0x00535770, CUnitClientSpecific::WeaponPoint);
