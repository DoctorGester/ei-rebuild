// A party unit's wounds, face texture and weapon point on the client (CUnitClientSpecific): the wound level
// of each body part and the composed wounds texture the figure shows called by RefreshWounds See
// unit_client_models.h.
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstdio>
#include <new>

// windows.h (through texture.h) renames CEngineObject's GetClassName.
#undef GetClassName

namespace {
constexpr int kBodyParts = 6;
constexpr int kTypeHero = 0x32;
constexpr float kFull = 1.0f;
constexpr float kMedium = 0.33f;
constexpr float kLight = 0.75f;
constexpr int kLegsLeft = 4, kLegsRight = 5, kHandLeft = 2, kHandRight = 3, kBody = 1, kHead = 0;
// armourIds: 0 the helmet, 1 the body armour, 2 the trousers.
constexpr int kHelmetSlot = 0, kBodyArmourSlot = 1, kTrousersSlot = 2;
constexpr std::uint32_t kNoArmour = 0xFFFFFFFF;

constexpr int kHeroTextureSize = 0x80;
constexpr int kTextureSize = 0x40;
constexpr int kMipCount = 3;
constexpr int kFormat = 0x4444;
constexpr const char* kWoundTexture = "%s%sw%d.mmp";
constexpr const char* kWoundCacheName = "mo%dw";
// The parts' texture names of a hero and of other units, each list ended by an empty name.
constexpr const char* kHeroParts[kBodyParts + 1] = /*0078F974*/ {"hd", "bd", "lh", "rh", "ll", "rl", ""};
constexpr const char* kParts[kBodyParts + 1] = /*0078F990*/ {"hd", "bd", "h", "h", "l", "l", ""};

// The level of one part (0 none .. 3 heavy).
std::uint8_t PartLevel(const UnitParamsSlot& part) {
    if (part.kind == 0 || part.maximum == 0) return 0;
    float maximum = static_cast<float>(part.maximum);
    float ratio = static_cast<float>(part.current) / maximum;
    EI_NAN_AUDIT(ratio, maximum);
    if (ratio == kFull) return 0;   // unhurt
    float stored = static_cast<float>(ratio);
    if (stored <= kFull / maximum) return 3;
    if (stored <= kMedium) return 2;
    if (stored >= kLight) return 0;
    return 1;
}

// Whether the armour piece `id` (packed: prototype index in the high half) shows the wounds
// under it. The record is read unchecked (an out-of-range index reads address 0xB4).
bool ShowsWounds(std::uint32_t id) {
    const auto* record = itemdb::ArmorAt(id >> 16);
    return record->showsWounds != 0;
}

std::uint8_t Worse(std::uint8_t a, std::uint8_t b) { return a > b ? a : b; }

constexpr int kWoundsTextureKind = 6;
constexpr unsigned kWoundsPreset = 4;
constexpr int kSourceAlpha = 5;          // D3DBLEND_SRCALPHA
constexpr int kInverseSourceAlpha = 6;   // D3DBLEND_INVSRCALPHA
constexpr const char* kNoWoundsTexture = "Can't create wounds texture for object \"%s\" ID = %d\n";

// *slot = texture with counted references; the old one released.
void AssignTexture(CTexture** slot, CTexture* value) {
    CTexture* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old) old->Release();
}

constexpr int kFaceTextureKind = 0x3D;
constexpr const char* kHeroFace = "%smouth_%.2d";
constexpr const char* kFace = "%smouth";

constexpr std::uint32_t kWeaponSlots = 4;
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFF;
constexpr const char* kHandNode = "baserh3";

} // namespace

bool CUnitClientSpecific::WoundLevels(std::uint8_t** levels) {
    bool wounded = false;
    for (int i = 0; i < kBodyParts; ++i) {
        std::uint8_t level = PartLevel(unit->params.slots[i]);
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

int CUnitClientSpecific::ComposeWoundsTexture() {
    int result = 0;
    {
        std::string name;
        std::string out;
        SkinRequest request{};
        // Six level bytes.
        request.layers.resize(kBodyParts);
        std::uint8_t* levels = request.layers.data();
        if (WoundLevels(&levels)) {
            bool hero = unit->type_ == kTypeHero;
            request.flags = hero ? kHeroTextureSize : kTextureSize;
            for (int i = 0; i < kBodyParts; ++i) {
                std::uint8_t level = request.layers[i];
                if (!level) continue;
                name = str::Format(kWoundTexture, unit->name, unit->type_ == kTypeHero ? kHeroParts[i] : kParts[i],
                            static_cast<int>(level));
                request.textures.push_back(name.c_str());
            }
            name = str::Format(kWoundCacheName, unit->id_);
            result = g_clientWorld->textureManager_->Compose(&out, &request, &name, &name, kMipCount, 0, kFormat);
        }
    }
    return result;
}

void CObject3DClientSpecific::HideWounds() {
    CEngineObject* oldTexture = woundsTexture;
    woundsTexture = nullptr;
    if (oldTexture) oldTexture->Release();
    ClearWoundsMaterial();
}

// The constructor: no references, then its own fields; the stages vector is constructed empty.
WoundsMaterial::WoundsMaterial() {
    blend = 1;
    sourceBlend = kSourceAlpha;
    destBlend = kInverseSourceAlpha;
    preset = nullptr;
}

int CObject3DClientSpecific::ShowWounds(const char* name) {
    if (!partSets[0]) return 0;
    CTextureManager* manager = g_clientWorld->textureManager_;
    if (!woundsTexture) {
        AssignTexture(&woundsTexture, manager->FindTypedTexture(kWoundsTextureKind, name));
        if (!woundsTexture) {
            AssignTexture(&woundsTexture, manager->FindOrCreate(kWoundsTextureKind, name));
            if (!woundsTexture) return game::FatalError(kNoWoundsTexture, object->GetClassName(), object->id_);
        }
    }
    std::vector<std::uint8_t> extra{};
    manager->LoadMmpExtra(name, &extra);
    const std::vector<std::uint8_t>& loaded = woundsTexture->extra_;
    bool same = !extra.empty() && std::ssize(loaded) == std::ssize(extra) &&
                std::memcmp(loaded.data(), extra.data(), static_cast<std::size_t>(std::ssize(extra))) == 0;
    if (!same) {
        woundsTexture->flags_ &= ~kTextureLoaded;
        if (!manager->LoadTexture(woundsTexture)) {
            AssignTexture(&woundsTexture, nullptr);
            woundsMaterial.Assign(nullptr);
            lib::FreeStorage(extra);
            return 0;
        }
    }
    if (!woundsMaterial) {
        woundsMaterial.Assign(new WoundsMaterial());
        CRenderHelper* helper = g_sceneRenderer->helper_;
        AsCountedRef(woundsMaterial->preset).Assign(helper->Preset(kWoundsPreset));
        woundsMaterial->stages.push_back(MaterialStage(partSets[0]->texture));
        woundsMaterial->stages.push_back(MaterialStage(woundsTexture));
    }
    lib::FreeStorage(extra);
    return 1;
}

// --- The face texture -------------------------------------------------------------------------

bool CUnitClientSpecific::LoadFaceTexture() {
    if (faceTexture) return true;
    const auto* prototype = unit->GetPrototype();
    if (!prototype) return false;
    char name[0x400];
    if (unit->type_ == kTypeHero) std::snprintf(name, sizeof(name), kHeroFace, unit->name.c_str(), prototype->skin);
    else std::snprintf(name, sizeof(name), kFace, unit->name.c_str());
    CTexture* texture = g_clientWorld->textureManager_->FindOrCreate(kFaceTextureKind, name);
    CTexture* old = faceTexture;
    faceTexture = texture;
    if (texture) ++texture->refCount_;
    if (old) old->Release();
    g_clientWorld->textureManager_->LoadTexture(faceTexture);
    CTexture* face = faceTexture;
    if (face->flags_ & kTextureLoaded) return true;
    faceTexture = nullptr;
    if (face) face->Release();
    blinks = 0;
    return false;
}

// --- The weapon point -------------------------------------------------------------------------

bool CUnitClientSpecific::WeaponPoint(Vector3* out) {
    std::string node;
    std::uint32_t slot = unit->activeWeapon;
    std::uint32_t weapon = slot < kWeaponSlots ? weaponIds[slot] : kNoWeapon;
    if (!WeaponBaseFigure(weapon, &node)) node = kHandNode;
    const FigureBone* found = unit->Object3D()->animation->FindBone(node);
    if (!found) return false;
    // The bone's position in figure space.
    const Vector3& position = found->position;
    out->x = position.x;
    out->y = position.y;
    out->z = position.z;
    return true;
}
