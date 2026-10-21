// The client controller's equipment slots: the armour and weapon ids it shows and the hero's
// composed skins after a change. See unit_client_models.h and docs/notes/units.md.
#include "unit_client_models.h"

#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "texture.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"

namespace {
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr int kArmourSlots = 7;
constexpr int kWeaponSlots = 4;

// The skin texture of a part set (CObject3DClientSpecific +0x20 / +0x24), or null.
CTexture* SkinTexture(const TextureParts* set) { return set ? set->texture : nullptr; }

// `texture` takes `name` and is loaded again (the original's inlined common tail).
void Reload(CTexture* texture, const mfc::CString& name) {
    texture->name_ = static_cast<const char*>(name);   // operator=(LPCTSTR)
    texture->flags_ &= ~kTextureLoaded;
    static_cast<CTextureManager*>(texture->owner_)->LoadTexture(texture);
}

// The original's test: a new skin, or a name that differs from the texture's (the texture read
// unchecked when `checkTexture` is false).
bool NeedsReload(void* composed, CTexture* texture, const mfc::CString& name, bool checkTexture) {
    if (composed) return true;
    if (checkTexture && !texture) return false;
    return gamecrt::mbscmp(static_cast<const char*>(name),
                           texture->GetName()) != 0;
}

CUnitController* AsController(CUnitClientSpecific* self) { return reinterpret_cast<CUnitController*>(self); }
} // namespace

int CUnitClientSpecific::RefreshSkins() {
    mfc::CString name;
    // EI_Plugin's Fixes (00536132): the test for a hero is gone, every unit is refreshed.
    if (unit->type_ != kUnitTypeHero && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) return 1;
    auto* half = unit->Object3D()->ClientSpecific();
    CTexture* detailed = SkinTexture(half->partSets[0]);
    if (NeedsReload(ComposeSkin(1, &name), detailed, name, false)) Reload(detailed, name);
    CTexture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name, true) && simple) Reload(simple, name);
    return 1;
}
EI_REPLACE_METHOD(0x00536100, CUnitClientSpecific::RefreshSkins);

int CUnitClientSpecific::RefreshSimpleSkin() {
    if (unit->type_ != kUnitTypeHero) return 1;
    mfc::CString name;
    auto* half = unit->Object3D()->ClientSpecific();
    CTexture* simple = SkinTexture(half->partSets[1]);
    if (NeedsReload(ComposeSkin(0, &name), simple, name, true) && simple) Reload(simple, name);
    return 1;
}
EI_REPLACE_METHOD(0x00536220, CUnitClientSpecific::RefreshSimpleSkin);

void CUnitClientSpecific::SetArmourModels(const std::uint32_t* ids, int count) {
    for (int slot = 0; slot < kArmourSlots; ++slot) AsController(this)->ArmourTakenOff(static_cast<std::uint32_t>(slot));
    for (std::uint32_t& id : armourIds) id = kNone;
    int n = static_cast<std::uint32_t>(count) < kArmourSlots ? count : kArmourSlots;
    for (int i = 0; i < n; ++i) AsController(this)->ArmourPutOn(ids[i]);
}
EI_REPLACE_METHOD(0x00538140, CUnitClientSpecific::SetArmourModels);

void CUnitClientSpecific::SetWeaponModels(const std::uint32_t* ids, int count) {
    for (std::uint32_t& id : weaponIds) id = kNone;
    int n = static_cast<std::uint32_t>(count) < kWeaponSlots ? count : kWeaponSlots;
    for (int i = 0; i < n; ++i) AsController(this)->WeaponAdded(ids[i]);
}
EI_REPLACE_METHOD(0x00538860, CUnitClientSpecific::SetWeaponModels);

bool CUnitClientSpecific::AddWeaponModel(std::uint32_t id) {
    if (id == kNone) return false;
    int slot = 0;
    while (slot < kWeaponSlots && weaponIds[slot] != kNone) ++slot;
    if (slot == kWeaponSlots) return false;
    if ((id & 0xFFFF0000u) == 0xFFFF0000u || (id & 0xFFFFu) == 0xFFFFu) return false;
    weaponIds[slot] = id;
    return true;
}
EI_REPLACE_METHOD(0x005388C0, CUnitClientSpecific::AddWeaponModel);

bool CUnitClientSpecific::RemoveWeaponModel(std::uint32_t id) {
    for (std::uint32_t& slot : weaponIds) {
        if (slot == id) {
            slot = kNone;
            return true;
        }
    }
    return false;
}
EI_REPLACE_METHOD(0x00538920, CUnitClientSpecific::RemoveWeaponModel);
