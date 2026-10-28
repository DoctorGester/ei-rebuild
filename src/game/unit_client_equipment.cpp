// The client controller's equipment slots: the armour and weapon ids it shows and the hero's
// composed skins after a change. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "texture.h"
#include "unit.h"

#include "lib/mfc.h"
#include "lib/str.h"

#include "ei/rebirth.h"

#include <string>

namespace {
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr int kArmourSlots = 7;
constexpr int kWeaponSlots = 4;

// The skin texture of a part set, or null.
CTexture* SkinTexture(const TextureParts* set) { return set ? set->texture : nullptr; }

// `texture` takes `name` and is loaded again (the common tail).
void Reload(CTexture* texture, const std::string& name) {
    texture->name_ = name.c_str();
    texture->flags_ &= ~kTextureLoaded;
    texture->Manager()->LoadTexture(texture);
}

// The test: a new skin, or a name that differs from the texture's (the texture read
// unchecked when `checkTexture` is false).
bool NeedsReload(void* composed, CTexture* texture, const std::string& name, bool checkTexture) {
    if (composed) return true;
    if (checkTexture && !texture) return false;
    return str::Compare(name,
                           texture->GetName()) != 0;
}
} // namespace

int CUnitClientSpecific::RefreshEquipment() {
    std::string name;
    // EI_Plugin's Fixes: the test for a hero is gone, every unit is refreshed.
    if (unit->type_ != kUnitTypeHero && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) return 1;
    auto* half = unit->Object3D()->ClientSpecific();
    CTexture* detailed = SkinTexture(half->partSets[0]);
    if (NeedsReload(ComposeSkin(1, &name), detailed, name.c_str(), false)) Reload(detailed, name.c_str());
    CTexture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name.c_str(), true) && simple) Reload(simple, name.c_str());
    return 1;
}

int CUnitClientSpecific::RefreshModel() {
    if (unit->type_ != kUnitTypeHero) return 1;
    std::string name;
    auto* half = unit->Object3D()->ClientSpecific();
    CTexture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name.c_str(), true) && simple) Reload(simple, name.c_str());
    return 1;
}

void CUnitClientSpecific::SetArmourModels(const std::uint32_t* ids, int count) {
    for (int slot = 0; slot < kArmourSlots; ++slot) ArmourTakenOff(static_cast<std::uint32_t>(slot));
    for (std::uint32_t& id : armourIds) id = kNone;
    int n = static_cast<std::uint32_t>(count) < kArmourSlots ? count : kArmourSlots;
    for (int i = 0; i < n; ++i) ArmourPutOn(ids[i]);
}

void CUnitClientSpecific::SetWeaponModels(const std::uint32_t* ids, int count) {
    for (std::uint32_t& id : weaponIds) id = kNone;
    int n = static_cast<std::uint32_t>(count) < kWeaponSlots ? count : kWeaponSlots;
    for (int i = 0; i < n; ++i) WeaponAdded(ids[i]);
}

bool CUnitClientSpecific::WeaponAdded(std::uint32_t id) {
    if (id == kNone) return false;
    int slot = 0;
    while (slot < kWeaponSlots && weaponIds[slot] != kNone) ++slot;
    if (slot == kWeaponSlots) return false;
    if ((id & 0xFFFF0000u) == 0xFFFF0000u || (id & 0xFFFFu) == 0xFFFFu) return false;
    weaponIds[slot] = id;
    return true;
}

bool CUnitClientSpecific::RemoveWeaponModel(std::uint32_t id) {
    for (std::uint32_t& slot : weaponIds) {
        if (slot == id) {
            slot = kNone;
            return true;
        }
    }
    return false;
}
