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

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

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
    EI_NAN_AUDIT(ratio, maximum);
    if (ratio == kFull) return 0;   // unhurt
    float stored = static_cast<float>(ratio);
    if (stored <= kFull / maximum) return 3;
    if (stored <= kMedium) return 2;
    if (stored >= kLight) return 0;
    return 1;
}

// Whether the armour piece `id` (packed: prototype index in the high half) shows the wounds
// under it. The original reads the record unchecked (an out-of-range index reads address 0xB4).
bool ShowsWounds(std::uint32_t id) {
    const auto* record = itemdb::ArmorAt(id >> 16);
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
EI_REWRITTEN(0x00536EA0, CUnitClientSpecific::WoundLevels);

int CUnitClientSpecific::ComposeWoundsTexture() {
    int result = 0;
    {
        mfc::CString name;
        mfc::CString out;
        // The request lives in raw storage so that its parts go in the original's order.
        alignas(SkinRequest) unsigned char storage[sizeof(SkinRequest)];
        auto* request = new (storage) SkinRequest{nullptr, 0, 0, 0, {}, 0};
        // 004ECB50: six level bytes in 32-byte aligned storage (the block kept before them).
        request->layers = static_cast<std::uint8_t*>(lib::AllocAligned32(kBodyParts));
        request->capacity = kBodyParts;
        request->layerBytes = kBodyParts;
        if (WoundLevels(&request->layers)) {
            bool hero = unit->type_ == kTypeHero;
            request->flags = hero ? kHeroTextureSize : kTextureSize;
            for (int i = 0; i < kBodyParts; ++i) {
                std::uint8_t level = request->layers[i];
                if (!level) continue;
                name.Format(kWoundTexture, unit->name.GetString(), unit->type_ == kTypeHero ? kHeroParts[i] : kParts[i],
                            static_cast<int>(level));
                request->textures.SetAtGrow(request->textures.GetSize(), name);
            }
            name.Format(kWoundCacheName, unit->id_);
            result = TheTextureManager()->Compose(&out, request, &name, &name, kMipCount, 0, kFormat);
        }
        request->textures.~CStringArray();
        lib::FreeAligned32(request->layers);
    }
    return result;
}
EI_REWRITTEN(0x005370F0, CUnitClientSpecific::ComposeWoundsTexture);

void CObject3DClientSpecific::HideWounds() {
    CEngineObject* oldTexture = woundsTexture;
    woundsTexture = nullptr;
    if (oldTexture) oldTexture->Release();
    ClearWoundsMaterial();
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

// A new material (0051C3D0 inlines its constructor).
WoundsMaterial* NewWoundsMaterial() {
    void* memory = gamecrt::operator_new(sizeof(WoundsMaterial));
    return memory ? new (memory) WoundsMaterial() : nullptr;
}
} // namespace

// The counted root's fields (the root's vtable is the compiled class's here), then its own; the stages
// vector is constructed empty.
WoundsMaterial::WoundsMaterial() {
    refCount = 0;
    blend = 1;
    sourceBlend = kSourceAlpha;
    destBlend = kInverseSourceAlpha;
    preset = nullptr;
}

int CObject3DClientSpecific::ShowWounds(const char* name) {
    if (!partSets[0]) return 0;
    CTextureManager* manager = TheTextureManager();
    if (!woundsTexture) {
        AssignTexture(&woundsTexture, manager->FindTypedTexture(kWoundsTextureKind, name));
        if (!woundsTexture) {
            AssignTexture(&woundsTexture, manager->FindOrCreate(kWoundsTextureKind, name));
            if (!woundsTexture) return game::FatalError(kNoWoundsTexture, object->GetClassName(), object->id_);
        }
    }
    AlignedBuffer extra{};
    manager->LoadMmpExtra(name, &extra);
    const AlignedBuffer& loaded = woundsTexture->extra_;
    bool same = extra.size != 0 && loaded.size == extra.size &&
                std::memcmp(loaded.data, extra.data, static_cast<std::size_t>(extra.size)) == 0;
    if (!same) {
        woundsTexture->flags_ &= ~kTextureLoaded;
        if (!manager->LoadTexture(woundsTexture)) {
            AssignTexture(&woundsTexture, nullptr);
            AsCountedRef(woundsMaterial).Assign(nullptr);
            extra.FreeData();
            return 0;
        }
    }
    if (!woundsMaterial) {
        AsCountedRef(woundsMaterial).Assign(NewWoundsMaterial());
        CRenderHelper* helper = TheSceneRenderer()->helper_;
        AsCountedRef(woundsMaterial->preset).Assign(helper->Preset(kWoundsPreset));
        woundsMaterial->stages.push_back(MaterialStage(partSets[0]->texture));
        woundsMaterial->stages.push_back(MaterialStage(woundsTexture));
    }
    extra.FreeData();
    return 1;
}
EI_REWRITTEN(0x0051C3D0, CObject3DClientSpecific::ShowWounds);

// --- The face texture -------------------------------------------------------------------------

namespace {
constexpr int kFaceTextureKind = 0x3D;
constexpr const char* kHeroFace = "%smouth_%.2d";      // 0078FC70
constexpr const char* kFace = "%smouth";               // 0078FC68
} // namespace

bool CUnitClientSpecific::LoadFaceTexture() {
    if (faceTexture) return true;
    const auto* prototype = unit->GetPrototype();
    if (!prototype) return false;
    char name[0x400];
    if (unit->type_ == kTypeHero) gamecrt::sprintf(name, kHeroFace, unit->name.GetString(), prototype->skin);
    else gamecrt::sprintf(name, kFace, unit->name.GetString());
    CTexture* texture = TheTextureManager()->FindOrCreate(kFaceTextureKind, name);
    CTexture* old = faceTexture;
    faceTexture = texture;
    if (texture) ++texture->refCount_;
    if (old) old->Release();
    TheTextureManager()->LoadTexture(faceTexture);
    CTexture* face = faceTexture;
    if (face->flags_ & kTextureLoaded) return true;
    faceTexture = nullptr;
    if (face) face->Release();
    blinks = 0;
    return false;
}
EI_REWRITTEN(0x00538DB0, CUnitClientSpecific::LoadFaceTexture);

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
    const FigureBone* found = unit->Object3D()->animation->FindBone(node.GetString());
    if (!found) return false;
    // The bone's position in figure space.
    const Vector3& position = found->position;
    out->x = position.x;
    out->y = position.y;
    out->z = position.z;
    return true;
}
EI_REWRITTEN(0x00535770, CUnitClientSpecific::WeaponPoint);
