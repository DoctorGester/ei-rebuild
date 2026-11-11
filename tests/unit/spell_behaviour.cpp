// The spells' behaviours over a fake record table (src/game/spells/spell_behaviour.cpp): the game's places keep their
// own behaviour and row whatever their fields say, a record past them or on a script reads its ai / damage_type /
// offensive / turns_victim / fighting / ai_effect, `effect` sets what its active effects do, and a table past 256
// spells is cut to 256. ei_unit_check (unit_check.cpp) and the POSIX port check run it.
#include "../../src/game/spells/spell_db.h"

#include <cstdio>
#include <string>
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
    const itemdb::SpellUse charm = UseOf(records[static_cast<int>(SpellType::Charm)]);
    Expect("charm: offensive, no AI use or damage, not turning the victim or fighting",
           charm.ai == SpellAi::None && charm.damageType == DamageType::Other && charm.offensive &&
               !charm.turnsVictim && !charm.fighting);
    const itemdb::SpellUse stun = UseOf(records[static_cast<int>(SpellType::Stun)]);
    Expect("stun (on Buff.lua): an empty row", stun.ai == SpellAi::None && !stun.offensive && !stun.fighting);
    const itemdb::SpellUse fireworks = UseOf(records[static_cast<int>(SpellType::Fireworks)]);
    Expect("fireworks (on Fireworks.lua): an empty row",
           fireworks.ai == SpellAi::None && !fireworks.offensive && !fireworks.fighting);

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

    // A record with a script is Scripted, a new one or one of the game's places, and reads its own fields.
    records[44].script = "pulse.lua";
    records[static_cast<int>(SpellType::Fireball)].script = "fireball.lua";
    itemdb::SetBehaviours(records);
    Expect("a script makes a record Scripted", records[44].behaviour == SpellType::Scripted &&
                                                    records[static_cast<int>(SpellType::Fireball)].behaviour ==
                                                        SpellType::Scripted &&
                                                    records[45].behaviour == SpellType::None);
    const itemdb::SpellUse scripted = UseOf(records[static_cast<int>(SpellType::Fireball)]);
    Expect("a scripted record reads its fields", scripted.ai == SpellAi::Heal && scripted.offensive &&
                                                     scripted.turnsVictim && scripted.fighting);
    // turns_victim and fighting given: their own, not offensive's (the curse magic's record: offensive, neither).
    SpellRecord& curseMagic = records[static_cast<int>(SpellType::CurseMagic)];
    curseMagic.script = "Lightning.lua";
    curseMagic.turnsVictim = 0;
    curseMagic.fighting = 0;
    itemdb::SetBehaviours(records);
    const itemdb::SpellUse curse = UseOf(curseMagic);
    Expect("turns_victim and fighting of their own", curseMagic.behaviour == SpellType::Scripted && curse.offensive &&
                                                         !curse.turnsVictim && !curse.fighting);

    // ai_effect: the AI's curse / protect / enhance test looks for the behaviour of the record it names, else for the
    // record's own; a game row's is its own whatever the field says.
    for (std::size_t i = 0; i < records.size(); ++i) records[i].code = "s" + std::to_string(i);
    const std::string slow = "s" + std::to_string(static_cast<int>(SpellType::Slow));
    records[44].aiEffectCode = slow;
    records[45].aiEffectCode = "nothing";
    records[static_cast<int>(SpellType::Teleport)].aiEffectCode = slow;
    const std::vector<std::string> unnamed = itemdb::SetBehaviours(records);
    Expect("an ai_effect naming no spell reported", unnamed.size() == 1 && unnamed[0] == "s45");
    Expect("ai_effect: the named spell's behaviour", UseOf(records[44]).effect == SpellType::Slow);
    Expect("ai_effect unknown or empty: the record's own", UseOf(records[45]).effect == SpellType::None &&
                                                               UseOf(records[46]).effect == SpellType::None &&
                                                               UseOf(records[static_cast<int>(SpellType::Fireball)])
                                                                       .effect == SpellType::Scripted);
    Expect("a game row looks for its own effect",
           UseOf(records[static_cast<int>(SpellType::Teleport)]).effect == SpellType::Teleport &&
               UseOf(records[static_cast<int>(SpellType::Healing)]).effect == SpellType::Healing);

    // effect: what an active effect of the record does (MapEffect::Behaviour), the kind it names, else its
    // behaviour's; ai_effect then looks for that (the named record's, else the record's own).
    SpellRecord& slowRecord = records[static_cast<int>(SpellType::Slow)];
    slowRecord.script = "Buff.lua";
    slowRecord.effect = static_cast<int>(SpellType::Slow);
    records[46].script = "pulse.lua";
    records[46].effect = static_cast<int>(SpellType::Stun);
    itemdb::SetBehaviours(records);
    Expect("effect: the kind it names", slowRecord.behaviour == SpellType::Scripted &&
                                            slowRecord.activeEffect == SpellType::Slow &&
                                            records[46].activeEffect == SpellType::Stun);
    Expect("effect not given: the behaviour's",
           records[static_cast<int>(SpellType::Fireball)].activeEffect == SpellType::Scripted &&
               records[static_cast<int>(SpellType::Stun)].activeEffect == SpellType::Stun &&
               records[45].activeEffect == SpellType::None);
    Expect("ai_effect: the named record's effect, else the record's own",
           UseOf(records[44]).effect == SpellType::Slow && UseOf(records[46]).effect == SpellType::Stun &&
               UseOf(slowRecord).effect == SpellType::Slow);
    // effectKind: the record whose script's OnStats gives the effect its stats.
    records[47].effect = static_cast<int>(SpellType::None);
    itemdb::SetBehaviours(records);
    Expect("effectKind: the named game spell's place, its own, none",
           slowRecord.effectKind == static_cast<int>(SpellType::Slow) &&
               records[46].effectKind == static_cast<int>(SpellType::Stun) && records[45].effectKind == 45 &&
               records[static_cast<int>(SpellType::Fireball)].effectKind == static_cast<int>(SpellType::Fireball) &&
               records[47].effectKind == -1);
    return failures;
}
