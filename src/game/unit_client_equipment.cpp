// The client controller's equipment slots: the armour and weapon ids it shows and the hero's
// composed skins after a change. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "item.h"   // kNoItemId
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "texture.h"
#include "unit.h"

#include <cstdint>
#include <string>
#include "lib/str.h"

#include "ei/rebirth.h"


namespace {
constexpr int kArmourSlots = 7;
constexpr int kWeaponSlots = 4;

// The skin texture of a part set, or null.
CTexture* SkinTexture(const TextureParts* set) { return set ? set->texture : nullptr; }

// `texture` takes `name` and is loaded again (the common tail).
void Reload(CTexture* texture, const std::string& name) {
    texture->name = name;
    texture->flags &= ~TextureFlag::Loaded;
    texture->Manager()->LoadTexture(texture);
}

// The test: a new skin, or a name that differs from the texture's (the texture read
// unchecked when `checkTexture` is false).
bool NeedsReload(bool composed, CTexture* texture, const std::string& name, bool checkTexture) {
    if (composed) return true;
    if (checkTexture && !texture) return false;
    return str::Compare(name,
                           texture->GetName()) != 0;
}
} // namespace

void UnitClientSpecific::RefreshEquipment() {
    std::string name;
    // EI_Plugin's Fixes: the test for a hero is gone, every unit is refreshed.
    if (unit->MapType() != MapObjectType::Hero && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) return;
    auto* half = unit->AsObject3D()->ClientSpecific();
    CTexture* detailed = SkinTexture(half->partSets[0]);
    if (NeedsReload(ComposeSkin(1, &name), detailed, name, false)) Reload(detailed, name);
    CTexture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name, true) && simple) Reload(simple, name);
}

int UnitClientSpecific::RefreshModel() {
    if (unit->MapType() != MapObjectType::Hero) return 1;
    std::string name;
    auto* half = unit->AsObject3D()->ClientSpecific();
    CTexture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name, true) && simple) Reload(simple, name);
    return 1;
}

void UnitClientSpecific::SetArmourModels(const std::uint32_t* ids) {
    for (int slot = 0; slot < kArmourSlots; ++slot) ArmourTakenOff(static_cast<std::uint32_t>(slot));
    for (std::uint32_t& armourId : armourIds) armourId = kNoItemId;
    for (int i = 0; i < kArmourSlots; ++i) ArmourPutOn(ids[i]);
}

void UnitClientSpecific::SetWeaponModels(const std::uint32_t* ids) {
    for (std::uint32_t& weaponId : weaponIds) weaponId = kNoItemId;
    for (int i = 0; i < kWeaponSlots; ++i) WeaponAdded(ids[i]);
}

bool UnitClientSpecific::WeaponAdded(std::uint32_t modelId) {
    if (modelId == kNoItemId) return false;
    int slot = 0;
    while (slot < kWeaponSlots && weaponIds[slot] != kNoItemId) ++slot;
    if (slot == kWeaponSlots) return false;
    if ((modelId & 0xFFFF0000u) == 0xFFFF0000u || (modelId & 0xFFFFu) == 0xFFFFu) return false;
    weaponIds[slot] = modelId;
    return true;
}

bool UnitClientSpecific::RemoveWeaponModel(std::uint32_t modelId) {
    for (std::uint32_t& slot : weaponIds) {
        if (slot == modelId) {
            slot = kNoItemId;
            return true;
        }
    }
    return false;
}
