// Unit setup and upkeep around creation: the prototype name index, the server controller's
// statistics hand-over and remarks, and dropping broken equipment after a hit. See
// docs/notes/units.md ("Creation, databases, save").
#include "game/units/unit_create.h"

#include "game/combat/combat.h"
#include "game/core/engine_object.h"
#include "game/item.h"
#include "game/item_manager.h"
#include "game/objects/map_object.h"
#include "game/players/player_messages.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr float kTenth = 0.1f;

PlayerServer* PlayerMessages(const Player* player) { return player->Server(); }

// The item's condition rounds to nothing.
bool IsBroken(const Item* item) {
    return lib::RoundEven(item->condition) <= 0;
}

// Through their game addresses (defined elsewhere; their verify wrappers stay in the path, and Rebirth's patches
// still run): the prototype table's name lookup (UnitPrototypeTable::FindByName, unit_create.cpp; inserts unknown
// names); the item manager's item by name (item_manager.cpp); Unit's EquipArmour /
// AddWeapon / AddSpell (unit_items.cpp, unit.cpp). The item manager's spell by name (with the world's manager
// context) is not defined here.

std::uint32_t Count(const std::vector<std::string>& names) { return static_cast<std::uint32_t>(names.size()); }

// A weapon name other than "none" (_mbscmp) and not empty.
bool Named(const std::string& name) {
    return str::Compare(name,
                           "none") != 0 &&
           static_cast<int>(name.size()) != 0;
}

// The weapon named `name`; a warning when it cannot be made (null is returned then).
Item* MakeWeapon(ItemManager* items, const std::string& name, const std::string& prototype) {
    Item* weapon = items->CreateItem(name);
    if (!weapon) game::Warning("Can't create weapon \"%s\" for unit \"%s\"", name, prototype);
    return weapon;
}

} // namespace

// --- Prototype names --------------------------------------------------------------------------

UnitPrototypeRecord* UnitPrototypeTable::FindByName(std::string_view name) {
    auto found = byName.find(std::string(name));
    return found != byName.end() ? found->second : nullptr;
}

// --- Server controller ------------------------------------------------------------------------

void UnitServer::SetStats(const UnitStats* source) {
    std::memcpy(&stats, source, sizeof(UnitStats));
    unit->MarkChanged(NetField::Stats);
    RefreshVitals(false);
    stats.RefillSlots();
    RefreshVitals(false);
    // The companion's vitals: hit points and mana at their maxima (the companion read again).
    auto* companion = unit->ServerCompanion();
    companion->vitals.hitPoints = companion->vitals.maxHitPoints;
    companion = unit->ServerCompanion();
    companion->vitals.mana = companion->vitals.maxMana;
}

void UnitServer::Shout(AckKind kind) {
    if (!IsShoutable(kind)) return;
    if (!unit->player) return;
    PlayerMessages(unit->player)->SendUnitSpeech(unit, kind);
}

// --- Broken equipment -------------------------------------------------------------------------

void Unit::DropBrokenEquipment() {
    Player* ownerObject = player;
    if (!ownerObject) return;
    // Each broken piece leaves its slot with the slot's reference, is given to the player, then released.
    std::vector<Item*> broken;
    PlayerServer* messages = PlayerMessages(ownerObject);

    for (ItemArmor*& slot : armor) {
        if (!slot || !IsBroken(slot)) continue;
        ItemArmor* piece = std::exchange(slot, nullptr);
        broken.push_back(piece);
        controller->ArmourTakenOff(static_cast<std::uint32_t>(piece->GetType()));
        MarkChanged(NetField::Equipment);
        MarkChanged(NetField::Stats);
    }

    bool weaponLost = false;
    for (int i = 3; i >= 0; --i) {
        Item*& slot = weapons[i];
        if (!slot || !IsBroken(slot)) continue;
        messages->SendWeaponRemoved(static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(i));
        broken.push_back(std::exchange(slot, nullptr));
        MarkChanged(NetField::Equipment);
        for (int k = i; k < 3; ++k) weapons[k] = weapons[k + 1];
        weapons[3] = nullptr;
        MarkChanged(NetField::Modes);
        weaponLost = true;
    }
    if (weaponLost) controller->ShowWeapon(0xFFFFFFFFu);   // re-pick the active weapon

    if (!broken.empty()) {
        ownerObject->GiveItems(broken, 1);
        for (Item* item : broken) item->Release();
        MarkChanged(NetField::Equipment);
    }
}

// --- The unit's lifetime ---------------------------------------------------------------------------

Unit::Unit() {
    // The unit's motion: two arrays, then the step speeds and the segment part.
    UnitMotion* m = &motion;
    // (The segment part's constructor only returns it.)
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    m->Clear();
    m->speedScale = kTenth;
    m->baseTurnRate = kTenth;
    Reset();
}

void Unit::Reset() {
    scriptName.clear();
    secondPrototype = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    unitRadius = kDefaultUnitRadius;
    standingHeight = kDefaultStandingHeight;
    relationsA = 0;
    relationsB = 0xFFFFFFFFu;
    side = 0;
    SetController(nullptr);
    animationEntry = -1;
    secondEntry = -1;
    player = nullptr;
    actionSound = 0;
    animationStart = 0;
    motionMode = 0;
    pathTimeShift = 0;
    moveMode = MoveMode::Walk;
    moveMode2 = MoveMode::Walk;
    pendingWeapon = -1;
    dead = 0;
    obstacle = 0;
    activeWeapon = 0xFFFFFFFFu;
    params.Reset();
    // The item slots and the carried list are empty here: they start so, and ReleaseUnit empties them before
    // every later Reset (Destroy, the destructor).
    altPassCosts = 0;
    AsMapObject()->netState.Reset();
    fightAtWill = kFightAtWill;
}

Unit::~Unit() {
    // The release-from-the-player virtual, called directly like the original (through the vtable's memory, not
    // the object's).
    ReleaseUnit();
    MapObject::Destroy();
    Reset();
}

// --- Starting equipment ------------------------------------------------------------------------

// A new unit's starting equipment from its prototype (the world adds zone units with it after
// ApplyPrototype). See unit_create.h (MapObjectManager) and docs/notes/units.md ("Creation helpers").

int MapObjectManager::GiveStartingEquipment(Unit* unit, const std::string& name) {
    SpellManager* spellOwner = context->GetSpellManager();   // passed on to the spell factory
    ItemManager* items = context->ItemFactory();
    auto* prototype = g_unitDatabase.prototypes.FindByName(name);
    if (!prototype) {   // a map's unit of a prototype the units database lacks (Lost in Astral's gz8g, gz17g)
        game::Warning("Can't find prototype \"%s\" for unit %d: no starting equipment", name, unit->id);
        return 0;
    }
    if (unit->MapType() == MapObjectType::Hero) {
        // The loop bounds are read again on every pass.
        for (std::uint32_t i = 0; i < Count(prototype->armourNames); ++i) {
            Item* armour = items->CreateItem(prototype->armourNames.begin()[i]);
            if (armour) unit->EquipArmour(armour);
            else game::Warning("Can't create armor \"%s\" for unit \"%s\"", prototype->armourNames.begin()[i], name);
        }
        if (Named(prototype->weaponName)) {
            int slot = unit->AddWeapon(MakeWeapon(items, prototype->weaponName, name));
            if (slot != -1) unit->pendingWeapon = slot;
        }
        if (Named(prototype->secondWeaponName))
            unit->AddWeapon(MakeWeapon(items, prototype->secondWeaponName, name));
    }
    for (std::uint32_t i = 0; i < Count(prototype->spellNames); ++i) {
        Item* spell = items->CreateSpellItem(prototype->spellNames.begin()[i], spellOwner);
        if (spell) unit->AddSpell(spell);
        else game::Warning("Can't create spell \"%s\" for unit \"%s\"", prototype->spellNames.begin()[i], name);
    }
    return 1;
}
