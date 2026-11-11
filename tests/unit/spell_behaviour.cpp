// The spells' behaviours over a fake record table (src/game/spells/spell_behaviour.cpp): the game's places keep their
// own behaviour and row whatever their fields say, a record past them reads its ai / damage_type / offensive, and a
// table past 256 spells is cut to 256. ei_unit_check (unit_check.cpp) and the POSIX port check run it.
#include "../../src/game/spells/spell_db.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void Expect(const char* what, bool got) {
    if (got) return;
    ++failures;
    std::printf("ei_unit_check: spell behaviour: %s\n", what);
}

} // namespace

int RunSpellBehaviourChecks() {
    using itemdb::SpellRecord;
    using itemdb::UseOf;

    // 300 records: the game's 42 and 258 a mod adds, each with ai, damage_type and offensive of its own.
    std::vector<SpellRecord> records(300);
    for (SpellRecord& record : records) {
        record.ai = SpellAi::Heal;
        record.damageType = DamageType::Crushing;
        record.offensive = true;
    }
    Expect("the records past 256 left out", itemdb::LeaveOutPast(records, kMaxSpellRecords) == 44 &&
                                                records.size() == 256);
    Expect("a table within the limit kept", itemdb::LeaveOutPast(records, kMaxSpellRecords) == 0 &&
                                                records.size() == 256);
    itemdb::SetBehaviours(records);
    Expect("the game's places their own behaviour", records[0].behaviour == SpellType::FireArrow &&
                                                         records[0x19].behaviour == SpellType::Stun &&
                                                         records[0x29].behaviour == SpellType::CurseMagic);
    Expect("the places after them None", records[42].behaviour == SpellType::None &&
                                              records[255].behaviour == SpellType::None);

    // The game's rows, whatever the record's fields.
    const itemdb::SpellUse fireball = UseOf(records[static_cast<int>(SpellType::Fireball)]);
    Expect("fireball: an area attack of fire, offensive and fighting",
           fireball.ai == SpellAi::AreaAttack && fireball.damageType == DamageType::Fire && fireball.offensive &&
               !fireball.turnsVictim && fireball.fighting);
    const itemdb::SpellUse stun = UseOf(records[static_cast<int>(SpellType::Stun)]);
    Expect("stun: a curse that turns the victim, no damage, not fighting",
           stun.ai == SpellAi::Curse && stun.damageType == DamageType::Other && stun.offensive && stun.turnsVictim &&
               !stun.fighting);
    const itemdb::SpellUse curse = UseOf(records[static_cast<int>(SpellType::CurseMagic)]);
    Expect("curse magic: offensive lightning, not fighting", curse.ai == SpellAi::Attack &&
                                                                 curse.damageType == DamageType::Lightning &&
                                                                 curse.offensive && !curse.fighting);
    const itemdb::SpellUse fireworks = UseOf(records[static_cast<int>(SpellType::Fireworks)]);
    Expect("fireworks: nothing", fireworks.ai == SpellAi::None && !fireworks.offensive && !fireworks.fighting);

    // A record past the game's reads its own fields; offensive also turns the victim and is fighting.
    const itemdb::SpellUse added = UseOf(records[42]);
    Expect("a new record's fields", added.ai == SpellAi::Heal && added.damageType == DamageType::Crushing &&
                                        added.offensive && added.turnsVictim && added.fighting);
    records[43].ai = SpellAi::None;
    records[43].damageType = DamageType::Other;
    records[43].offensive = false;
    const itemdb::SpellUse plain = UseOf(records[43]);
    Expect("a new record without the fields: nothing", plain.ai == SpellAi::None &&
                                                            plain.damageType == DamageType::Other &&
                                                            !plain.offensive && !plain.turnsVictim && !plain.fighting);
    return failures;
}
