# Items, spells and related data (texts, textures, registry files, trader stock, party)

Layouts are in the headers (`item_db.h`, `spell_db.h`, `item.h`, `spell.h`, `item_stock.h`, `party.h`, `texture.h`); this file holds
what they do not say. The HUD panels and windows are in `ui.md`; players and their messages in `objects.md`.

| Subsystem | Code | Scenario |
|---|---|---|
| Item and spell databases | item_db, spell_db | all (startup) |
| Item rules, construction, names | item, item_manager, item_names | items, ui_camp |
| Item texts | text_manager | items |
| Spell rules, construction | spell, spell_manager | spells |
| Spell effects | spell_effect, spell_effect_update | spells, spells_cast, spells_more |
| Scripted spells | script/script_spell_host | mod_spell_script, `--lua-check` |
| Quest database | quest_database, quest_records | any new game in zone1 |
| Textures (mmp, composed) | texture_load, texture | all (skins at zone load) |
| Registry files, ini | registry, ini_file, settings | all |
| Trader stock | item_stock, class_templates | trader_stock |
| Party, records, item lists | party*.cpp | ui_camp, trader_stock, party_records, items |

## Databases and naming

- Eight tables of fixed-size records (materials, weapons, armors, quick items and spell containers, quest items, loot
  items, spells, spell modifiers), each an `itemdb::RecordTable<Record>`: a vector plus an index by `IndexKey` (a
  material's key, an item prototype's name, a spell's or a modifier's code). `At` checks `index < (end - begin) /
  stride` (signed division, unsigned compare), else null. The original's callers read fields of the null record and
  faulted. Ours, one rule:
  - **An item's prototype, and a spell's prototype and modifiers, are always there.** Every item comes from
    FillItem (which makes none without a prototype), CopyTo (a copy of one that has it) or a loader; every spell
    from the spell constructor (database records), CopyTo or a loader. The loaders test what they loaded
    (`HasEveryRecord(context)`: each item's `Item::HasPrototype`, each spell's `Spell::HasRecords`, the prototype
    and every modifier code; `Spell::Load` reads the modifiers, the identity records or the sender the packed id).
    An item or spell without its records comes from a save, a party file or a player of another items.idb or
    spells.sdb. So `GetType`, `GetTypeGroup`, both `GetPrototype`s (the item's null only for scrolls), the modifier
    readers and the texts read the records unchecked.
    - Our own data ends the game (`RequireEveryRecord`: `game::FatalExit`, the error box and exit): the world save
      (after the identity records, before the map objects load,
      since a unit's load reads its weapon's type; again after the objects' own data), a saved spell effect's
      spell, the party files, the save's party records and trader stocks, the players' parties in a save.
    - A peer's data is refused where the loader can undo it: the world server drops a player whose party
      (`OnNewParty`, `OnPartyUpdate`) names records it lacks (message 0x11 with the reason; the player leaves),
      and a trade partner's offer naming them leaves the partner's party empty with a chat line
      (`ClientSession::ApplyParty`).
    - A client refuses the world server's blocks naming them: `WorldClient::LoadItemsAndSpells` /
      `LoadNewItemsAndSpells` empty the client world's item and spell managers (`KeepItemsAndSpells`), the party
      messages 0x80 / 0x83 the party (`PlayerClient::KeepParty`; the heroes' folder is not written), and the
      handler makes the session leave (`LeaveAfterTick`), as message 0x11 does: one path, `objects.md`
      ("Leaving the server"). `net_pair_database` checks it (a host with one quest item more).
  - **Every other index is tested where it is read**: a packed id's material half, the record a loot item stands for
    (`ItemLoot::HasRecord`), the packed ids a unit's figure gets (unit skins, the client's armour and weapon ids).
    Without the record: the packed-id weapon stats give 0 (type NoWeapon, damage type 6), names and text keys are
    empty, a loot's price -1, an id dresses or animates nothing. The camp's predicates (`IsPart`, `IsMaterial`,
    `IsSpell`, `IsModifier`) and the item info's dispatch hold only with the record, so a loot item without one is
    plain loot there and the code behind them reads the record unchecked. A script's unknown name ("Instruction
    Weapon.nosuch") makes such a loot item too.
  - Open: a weapon's or armour's `type` is not checked at database load, and the armour type
    indexes the unit's seven slots (every install's and mod's items.idb has 0-6). `Named` files a null record
    under an unknown key. `IndexOf` is -1 for a null record (the original divided the null record's address
  offset by the record size): only a database without a "spell container" quick item reaches it, and the scroll
  makers (CreateSpellItem, the camp's) then pass -1 to MakeItem, which takes it as index 0.
- Loading (startup and each switch of the resource database): "items.idb" (materials, weapons, armors, quick items,
  quest items, loot) and "spells.sdb" (spells, modifiers, three recipe lists) from database.res, each table through a
  ChunkArchive over the file's memory stream; each record filed in the index (a material under its key, an item
  prototype under its name, a spell or modifier under its code; a later record of the same key replaces the earlier). A
  modifier's code is a `char[4]` read as a C string. Lists inside records are emptied and refilled with one value per
  field chunk. Nothing copies a record (the tables are resized from empty while loading). Either file may be TOML
  instead, or a TOML file may merge records over it by key, before the records are filed (mods.md, "Databases as TOML").
- Quick item categories (field 0x15, `QuickItemCategory`): 5 for "wand 1".."wand 6" (the spell is put in, casts pay from
  the charge), 8 for the potions and the scrolls' "spell container" (the spell comes from spellText; potions are used up
  and only on the user), 0 for the danger sensor; 2 (`Consumable`) has no prototype in the data. A loot prototype's
  field 0x15 is its `LootKind` (2 for the "instruction ..." blueprints). An armour's field 0x15 is its first
  absorption row (`ArmorRecord::firstFactor`, the row, the flags): the original kept that factor's bits in the head's
  category, so `Item::GetCategory` of an armour read them; ours leaves the category 0. No category test can tell: the
  callers compare with 2, 5 and 8, and no install's or mod's items.idb (192-215 rows, factors 0.63 and up) has a
  factor with those bits.
- Armour types (`ArmorType`, the codes "hl", "pl", "lg", "sh", "pt", "bt", "gl"; also the unit's armour slot):
  Helmet, BodyArmor and Leggings have models (the figure's helmet, body and trousers rows), layer group 2 and the
  "upper" text keys (`IsUpperBodyArmor`); Shirt, Pants, Boots and Gloves go under them (group 1, no model parts).
  Shirt and Pants are read from their codes and covered body slots (body and arms; legs), not from a game text.
- Naming: 0x3007 is the loot item ("litem", ItemLoot) and 0x3009 the quest item ("questitem", ItemQuest).
- Weapon stats: min damage = weapon damage x material damage, max = (spread + damage) x material damage, attack =
  weapon attack x material attack, damage type = the first non-zero weight (6 if none).

## Item classes and rules

- `Item : EngineObject` and six classes: weapon, armour, quick item, quest item, loot, scroll (`ItemScroll`, a "spell
  container", derived from the quick item). `ItemManager` is the factory (kinds 0x3004..0x3009); the subclasses' own
  fields start as the heap had them (`new ItemQuick`, not `()`) until InitFromRecord or Load sets them.
- `Item::unreadValue` (archive field 9): saved, loaded and compared by IsSameAs; nothing else reads it.
- Load / Save: the base fields, the spell's id (-1 none; Load looks it up in the context's spell manager); armour adds
  its two absorption rows. IsSameAs compares the base floats as "equal or unordered" and the armour's rows and masks as
  dwords.
- Making an item: `ItemConstructor::BuildItem(manager, kind, prototype, material, spell, count)`. Only the trader
  stock's GenerateItems runs the steps itself: it creates the item before it draws the material, and a cancelled draw
  has used up an object id (moving the creation would shift every later item id). Object ids are per manager (maxId + 1).
- `RecordModelName` builds model names ("%s_%.2d.%s.%d"; "" without a prototype or material); an unknown weapon / armour
  type gives "".
- Clone = the owning manager's CreateObject(kind, -1), then the source's CopyTo, which gives the target the packed id,
  condition..magicLevel (bit for bit), a clone of the attached spell and count 1.
- Kept: the loot's Load reads kind / recordType through its stream argument's slot, Item's the spell id likewise.
- The classes are told apart by `AsWeapon` / `AsArmor` / `AsLoot` / `AsQuick` / `AsQuest`, which test `Kind()` (null
  for another kind; a scroll is a quick item): the item manager makes each kind's class, and the kind never changes.
  `GetUse` is the quest item's key by kind too; an item's two link records are typed fields: `serverLink` (the world server's `ItemLink`) and `turn`
  (the item views' `ItemTurn`).
- Original bug kept: `Player::HasLoot` (over a unit's carried items) and the script's `RemoveQuestItem` compare the
  quest item id's word of every carried item without a class test: a weapon's or quick item's actionValue, the
  armour's first absorption value (its bits) or the loot's kind can match an id (`Item::QuestWord`). A fix would
  test the class first.

## Item texts

Text keys (record name with ' ' replaced by '_', upper-cased): 0x3004 `weapon NAME MATERIAL` (kind text item_weapon),
0x3005 `armor NAME MATERIAL` (item_armor_upper for type 0-2, else item_armor_lower), 0x3006 `qitem NAME` for a "none"
prototype (item_potion) else `qitem NAME MATERIAL` (category 5 only: item_wand, name + ": " + spell name, "Unknown"
without text), 0x3007 by kind: 0 `litem NAME` (item_loot), 1 `material MATERIAL` (item_material), 2 by record type: 0-2
`instr NAME` (item_weapon_shape, item_armor_shape_upper / _lower, item_wand_shape), 3 `spell ` + code (item_prototype), 4
`modifier ` + code (item_modifier); 0x3008 `spell ` + its prototype (item_spell); 0x3009 `questitem NAME` (item_quest).
The kind text is `string <kind key>`; the display name is `format_item1` (kind, name) or `format_item2` (kind, name,
count) above a stack count of 1. GetString on a missing text gives the key itself (shown as is); the text manager never
closes files from GetFile (reference counts only grow).

## Spells: rules and construction

- Target flags (`SpellTargetFlag`): 1 not hostile units only, 2 hostile only, 4 not humans ("unhuma" / "unhufe"), 8 not
  "unmogo", 0x10 not orcs ("unorma" / "unorfe"), 0x20 not "unmoli" (modifiers "ff", "fe", "fh", "fg", "fo", "fl");
  0x10000 / 0x20000 the cast-flag modifiers "it" (an item casts it on a hit) / "ic" (an item's lasting spell);
  0x10000000 / 0x20000000 the prototype's target kind 'u' / 't'.
- Damage types (`DamageInfo`): spells 0/3/6/0x27/0x28 deal type 3, 1/4/7/0x29 type 5, 2/5/8 type 4; a hit on several
  targets scales amount and multiplier by 4/n.
- **Cast chain.** Text casts (`CastSpellUnit("healing{e2;e2}", ...)`) go through CreateSpellFromText (the text before `{`
  is the prototype, `;`-separated codes are modifier names), then as CreateSpell: the SpellConstructor, BeginConstruct
  (a new container, class 0x3003), AddModifier per modifier (at most 8), accepted when AllowsModifier says so (the
  prototype's flag bytes r, t, a, e, d; other letters always), the spell rebuilt each time; EndConstruct rebuilds once
  more; Spell::Clone makes the result. **Build** recomputes the container's spell from scratch (offset mapping and
  modifier letters a, e, m, r, d, t, fe/ff/fg/fh/fl/fo, ic/it in `spell_manager.cpp`); the code string is sorted.
  SpellEffect's `targetCount` is the duration (for attack spells it may be a target count), `damage` the effect.
- Kept: LoadSpell reads flags through its stream argument's slot; Build leaves the target flags alone when the prototype
  kind is neither 'u' nor 't', so they accumulate until Reset; a modifier whose code is not found becomes code 0, which
  sorts first and empties the modifier list; the container's AddModifier reports a null record as "Invalid rune".
- `CreateSpellObject` takes no id (the original's only caller passed -1; a packed id with a low half other than 0
  ended the game: there are no spell sequences). `Spell` takes its numbers as the `SpellValues` base (CopyTo assigns it). `Light`: range sqrt(FLT_MAX);
  `LightFactory` makes lights 0x2E / 0x31.

## Spell behaviours

A spell record's place (Spell's `prototypeId >> 16`, `SpellValues::prototypeIndex`, `MapEffect::type`) says which
record it is; its behaviour (`SpellRecord::behaviour`, `Spell::Behaviour()`, `MapEffect::Behaviour()`) says what it
does. The load (`SpellDatabase::Read`, `SetBehaviours`) gives the game's 42 records the behaviour of their place, so for
them the two are one number and every golden stays. A record with a `script` gets `SpellType::Scripted` (any place;
"Scripted spells"), any other record after them (a mod's: the showcase's "spark", 42) `SpellType::None`, which starts
nothing.

- What reads the behaviour: `SpellEffect::Start`, `UpdateMissile`, `UpdateLasting`, `UpdateFootprint`, `DamageKindOf`
  (spell_effect*.cpp), the cut spells in `CastSpell`, `FillCastKind`'s Rick / curse magic, the healing item triggers
  (combat_actions, combat_update), the effects' stat reads (`HasEffect`, `EffectValue`, `AddEffect`'s scale and stun,
  unit_vitals' strongest per behaviour) and the client's looks and sounds (`MergesParticles`, `ParticleSourceType`,
  `SoundFolder`, map_object_client_effects.cpp). A behaviour without a look (None) gets no particle source and no
  log. The HUD's buff icons and the AI's "has it already" option effect keep the record (icon) and the behaviour.
- What the tables say of a spell is `itemdb::UseOf(record)` (`Spell::Use()`, spell_behaviour.cpp): the AI's option
  kind (`ai`), the damage type of the AI's estimate (`GetSpellDamage`), `offensive` (the HUD's use at once aims at the
  nearest enemy), `turnsVictim` (`CreateMissile`: the victim sees the caster, diplomacy counts an attack) and
  `fighting` (`UnitServer::IsFighting`: the battle music). The game's 42 have one row each (`kGameSpells`, with
  Rebirth's FixSpellSelfCasting and FixPlaingBattleMusicFromAntimagic in it); a Scripted or None record reads its TOML fields
  `ai`, `damage_type` and `offensive` (offensive also standing for turnsVictim and fighting). The fields of a game
  record are not read: its row is its behaviour's.
- Limits: a spell's place is a byte (prototypeIndex, MapEffect::type, saved and sent), a modifier's place + 1 a signed
  byte (`Spell::modifiers`); the load leaves out spells past 256 and modifiers past 127 with a warning.
- Moving the game's spells onto the scripted path later (their records' rows as fields) needs `turnsVictim` and
  `fighting` to be fields of their own: the game's rows differ from `offensive` there (Lichdom and Charm are
  offensive and turn nobody; Rick and curse magic are not fighting).
- Checked by ei_unit_check's spell behaviour cases (tests/unit/spell_behaviour.cpp: a fake 300-record table cut to
  256, the game's rows whatever the fields say, a new record's fields, a script making any record Scripted) and by
  every golden staying the same.

## Scripted spells

A spell record with `script = "<file>.lua"` (TOML only, `SpellRecord::script`) is Scripted, wherever its place: the
file below `Res\spells\` runs its casts. It is meant as the one spell path: the game's own spells are to move onto it
(MF17g), so nothing in it is a mod's alone. Code: `ScriptSpellHost` (src/game/script/script_spell_host.h), one per
scheduler (`ScriptLuaScheduler::spells`); the casts use the world's main script's (`MainScript()->Runner()`), the
zone's server state that Eris saves. Only the server runs it; clients see what it makes (MF17c's bindings).

- **The file** is a module: it returns a table of hooks, named in the game's bindings' style. `OnCast(cast)` once, at
  the cast's start; `OnTick(cast)` every tick after (optional: a module with it keeps its casts alive); `OnEnd(cast,
  reason)` once, before the cast goes, `reason` "finished", "idle", "limit" or "error"; `OnHit(cast, target, payload,
  point)` when a missile of the cast arrived (`target` nil when its object is gone); a timer's function by the name
  `cast:After` gave it (`module.<name>(cast, payload)`). The hooks are looked up by name at each call, so a fixed file
  reaches the casts of a save.
- **Loading.** A file is found through the layers (`FilesByLayer("Res\spells", {file})`, the top layer that has it)
  and loaded at the first cast that needs it in a state (so again after a restore: the modules are not saved), into
  globals of its own that read the state's (`setmetatable({}, {__index = _G})`), so it sets no mission global. Its top
  level runs with every binding off (`ScriptLua::worldOff`: a binding is a Lua error), so when it loads cannot matter.
  A file that does not load (missing, a Lua error, no table) says `Script error: Res\spells\<file> did not load: ...`
  once per state; its casts end at once with no hook.
- **The cast value**, a new table for each hook: `id` (the key), `code`, `caster`, `target` (objects or nil), `from`,
  `point` (`{x, y, z}`), `tick` (ticks since the cast; 0 in OnCast), the built `damage`, `range`, `radius`,
  `duration`, `targetCount`, `speed`, `mana`, `power` (`spellfx::Power`), `school` (recordnames::kSpellSchools), and
  `data`, the script's own table (a new one assigned to `cast.data` is kept). Values are lower camelCase, methods
  CamelCase: `cast:After(ticks, "Name" [, payload])` (at least 1 tick, whole ticks), `cast:Finish()` (the cast ends
  after this hook), and the world methods below.
- **The world methods** (the modders' table: docs/modding.md, "Spells"), each on the code the game's spells call, so
  that MF17g can move them onto these: `FindUnits` (`sectorMap.FindUnitsInRadius`, sorted by distance then id;
  filters `SpellHostile` (CanAffect's ff / fe test), `IsDead`, `AcceptsAreaSpells`, `SpellHasLineOfSight`, HitUnit's
  affect test, an id set), `CanAffect` (that test), `Damage` (`SpellEffect::TypedHit`, GetDamage's hit, then
  `HitUnit`; `noCredit` sets flags bit 0 for the call, as the walls' later pulses), `Heal` (`controller->Heal`),
  `Apply` / `Remove` / `Has` (`BuffRecord`, ApplyBuff's record, with the kind's place as its type: a spell's code or
  `"own"`; `MapObject::RemoveEffects` drops a type's records through `EffectRemoved`, the expiry's path), `Move`
  (`MoveUnit`, the teleport's check and move, at the unit's height, then `ResyncWithUnit`), `Bolt` (`NewLightning`,
  `LightningEnd` for a unit end), `Missile` (`NewArrow`, the fire arrow's EffectArrow: its speed is the arrows' fixed
  one, so the design's `speed` is not taken), `Effect` (`NewParticle`; names through `ScriptParticleTypeByName`, the
  CreateParticleSource table then the data effects), `Sound` (`CreateSound`), `Light` (`NewPointLight`). Arguments are
  all read (and a Lua error raised) before any C++ object is made: Lua is C, its errors are longjmps.
- **Owned objects** (`SpellEffect::owned`, `SpellOwned`): every bolt, missile, effect and light a cast makes is in the
  world's timed objects and held by the effect with its ticks left (0: as long as the cast) and, for a missile, the key
  of its payload in the entry's `missiles` (each payload wrapped in a table, so nil is one). Sounds play once and are
  not held. `TickOwned` each tick: a lifetime run out removes the object, one that left the timed objects on its own
  (a one-shot particle) is dropped, a missile that `arrived` goes and its OnHit is due. The cast's end releases them
  all (`ReleaseOwned`, at the effect's ended update and in its destructor).
- **The order** (`SpellEffect::Update`, in the world tick's projectile walk, in list order): state 0 runs `Start`,
  whose Scripted case makes the entry and calls OnCast, then state 4 (`kStateScripted`); each later update counts the
  tick, runs the due timers one at a time by due tick, then by the order they were set in (a timer set by one is due
  the next tick at the earliest), then OnTick, then the end test; before the timers, the owned objects' lives and an
  OnHit per arrived missile in the list's order. The end: Finish after any hook (the rest of that tick's hooks
  skipped), the tick cap (the spell's `duration`, else 600 ticks), or nothing pending (no timer, no owned object with
  ticks left, no missile in flight and no OnTick: "idle"); a hook's Lua error goes to the console (`Script error: <file>:<line>: <message>`), then OnEnd(cast,
  "error"). The effect then ends as any spell does (state 3, removed at its next update).
- **Hooks are not coroutines**: they run with `lua_pcall` on the state's main thread, so `Sleep` and the other waits
  raise an error (no yield outside a coroutine). Timers and OnTick do the pacing (user 2026-11-11). The bindings work
  in them; their commands run in a run of the host's own.
- **CastSpell** makes one effect for a Scripted spell, whatever its target count and filters (the script chooses its
  targets, MF17c). The victim turning on the caster is `offensive`'s (UseOf). `Forget` keeps a scripted cast when its
  target goes (`target` cleared); the caster is cleared as before.
- **Saves.** The host's `casts` table (key: `data`, `timers` = `{due, seq, name, payload}`, `missiles` = `{[key] =
  {payload}}`, `tick`, `seq`, `finished`) and `nextCast` are in the scheduler's persisted table; the cast value's
  metatable and its methods are permanents (`ei.cast`, `ei.cast.After`, ...). A state-4 `SpellEffect` record adds its
  key and its owned objects (a count, then each one's timed-object id, ticks and payload key) after the old fields
  (tools/savefile_items.py `castKey`, `owned`); the load finds them in the timed objects. The save version stays 0x74: only a state-4
  record has the fields, which no older save holds, and a bump would change every decoded save (the version is in the
  decoded text); MF17g, when vanilla saves can hold scripted casts, may want it so that older builds refuse them.
  After `MainScript()->LoadState` each state-4 effect finds its entry (`SpellEffect::ResumeScripted`,
  `ScriptSpellHost::Adopt`); one the restored state lacks ends with the console's `Script error: the save's Lua state
  has no cast ...`.
- **Zone teardown.** The host goes with its scheduler (`Script::Reset`); its live casts' effects let go of it and end
  at their next update, with no hook. An effect that goes first (the projectile list cleared) drops its entry.
- **Mods in multiplayer**: `Res\spells\` counts in the mod's content hash (mods.md, "Mods in multiplayer").
- **Checks.** `--lua-check`'s host cases (tools/convert/lua_check.cpp `CheckSpellHost`: timers in order with payloads,
  Finish, OnTick to the cap, idle, a hook's error, a wait in a hook, no bindings at the top level and the file said
  once, a timer naming no function, a cast saved after each of its first six ticks ending as the unbroken one; the
  world methods on a stone in a world without a map: their arguments and errors, Apply / Has / Remove, what is owned
  and when it goes, OnHit with payloads, the idle end); `mod_spell_script` (both installs; tools/make_lua_mod.py
  spell): a console cast of a timer spell, saved and loaded mid-cast, its count going on to the same end tick as an
  unbroken run; `mod_spell_api` (both installs; make_lua_mod.py spellapi): zone1's wolf 1804 casts a spell that uses
  every world method on a spawned wolf, two missiles with OnHit and a data effect for 300 ticks, saved and loaded
  mid-cast, ending idle at tick 300.

## Spell effects

`SpellEffect` (world.h calls it `Projectile`) is one cast spell and embeds its Spell. The world server ticks `Update`;
the state: 0 casting (`Start`, a switch on the prototype whose `Start*` functions set the state themselves: the
missile, the lasting one through `EnterLasting` or `kStateEnded`; the charm leaves it at 0 and so starts again every
tick), 1 missile in flight (`UpdateMissile`), 2 lasting (`UpdateLasting`), else it ends (footprint cleared, the three effect objects dropped). The three counted effect objects
(visual, sound, light) are also in the world's timed objects; replacing one removes and releases the old.

Spell behaviours: 0 / 0x28 fire arrow, 1 / 0x29 lightning, 2 acid ray, 3 fireball, 4 light strike, 5 acid column, 6 fire
wall, 7 light wall, 8 poison cloud, 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26 buffs (the target's AddEffect gets a
record with byte 1 = trunc(power x 16)), 0x12 fireworks, 0x14 clairvoyance, 0x15 possession, 0x17 vision fog, 0x18
healing, 0x1B link, 0x1C teleport, 0x24 charm, 0x27 campfire. `SpellType` names all 42 after the spell database's
prototypes (0x16 shapechange and 0x23 comprehend start nothing).

"Power": `pow(clamp(damage / prototype effect, 1, 8), 0.33) - 0.2`, or 1 when the prototype's effect compares equal to 0
(NaN included); the clamps let NaN through. The fireball blast uses the same curve on `(r*r*pi) / prototype area`.

Lights: the prototype's fields 23-27 are its light (`lightColor`, `lightRange`, `lightRiseDivisor`); the lights rise at
`lightRange / max(lightRiseDivisor, 1)` a tick, the impact lights 1.5 times faster and further.

Hits: HitUnit skips units the spell may not affect (only checked with a caster) and calls the controller's TakeHit;
HitUnitsInRadius (fireball) and HitUnitsAlong (fire wall, every 4th tick) hit units that area spells hit (size class
set, or the prototype's height below 0.5); the wall only those within 0.5 of the vertical plane along its direction.

Kept: lightning builds its light at the target's position without checking the target; healing and charm read the
target's class without a null check; arrow and acid ray impacts read the target after the hit unchecked; charm with a
target or caster that is not a unit returns without changing state, so it restarts every tick; the light strike uses -7
as bolt width factor; the walls compute the normal with a z of 0 * k.

Cut spells (H43b): WorldServer::CastSpell returns for possession, link and charm, so StartPossession, StartLink,
StartCharm and their updates (Possession, Link) run for no cast; `spells_more` casts charm and link from the hero and
checks that the wolf stays an enemy (the cast spends mana). Fireworks (a script cast) and teleport (the hero, then a
ground click) run in `spells_more`; the teleport moves the hero when the effect's timer comes down to the duration.

## Unreached in the shipped game

`StartCharm`, `StartLink`, `StartPossession` (spell_effect.cpp) and the Link and Possession updates
(spell_effect_update.cpp) stay with a one-line comment: `WorldServer::CastSpell` returns for those three types, so
only a saved projectile of one would start them. `GiveBriefingRewards` (world_quests.cpp, creating items with `CreateItemNamed`) stays
too: `CompleteBriefing` returns before them in any network game (the host is a client too) and a single-player game
never holds a briefing record.

## Quest database

`QuestDatabase::LoadRegistry` (from the quest registration): a record is added unless the name index already holds it
with a record; after each push_back the index is rebuilt over the whole vector. One briefing record is filled three
times ("briefing receive", "reject", "complete"). In saves: `QuestRecord` and `QuestBriefingRecord` through
ChunkArchive. With more than 16 quests and unknown names the objectives' sort runs past the array (the original too).

## Textures

- `.mmp` files (`CreateFromMmp`, `MmpHeader`); `LoadBmp` takes an alpha "a_<file>" beside a loose .bmp. `SetDevice`:
  quality to the mip level, the cache limit a tenth of 256 MB (`kCacheLimit`); `TrimCache` every 32 client ticks.
- The texture manager's Create opens "textures.res", "redress.res", "menus.res" of the text folder (1 only when all
  three opened). CreateObject sets flags 0x80 / 0x100 / 0x200 for types 0x3E / 0x3F / 0x40, else 0x40.

### Composed textures

TextureManager::Compose (texture_load.cpp): a party unit's skins ("mo<n>p" / "prot<n>p" 256 square and "mo<n>s" /
"prot<n>s" 128) and its wounds texture ("mo<n>w", the wounded parts' "<unit><part>w<level>.mmp" overlays, 128 for a hero
and 64 for others) are drawn from layer textures on the GPU (`render::ComposeTexture`; renderer.md). The texture manager
keeps what each one is made of under its name, case-insensitive (`ComposedTexture`: the header the original's cache
file had, the request's layer bytes as its extra block, the layer textures). An unchanged request is reused; when the
name has none and differs from the fallback name, the fallback's is tried; no layer textures forgets it; a missing layer
is skipped (logged), one of another size too (a warning in the log). `Texture::CreateComposed` hands the layers' file pixels
to the renderer, which decodes them (32-bit premultiplied, "PNT3" run-length packed), draws them over transparent black
and makes the levels below with a box filter, 8 bits a channel. `ForgetComposedTextures` (map load, texture manager
create / destroy, shutdown) drops them all.

## Materials

`MaterialManager`: a table from a key to its colours (an absent key is entered with the key's bits in all 17 dwords);
Create adds "materials.res" and enters the eight kinds (0x11 leather .. 0x17 magic, 0x19 metal again) with their file's
17 dwords, or diffuse 0.3 / alpha 1, ambient 0.7, specular 0.2, power 5 without the file (magic's emissive is 1).
NewMaterial returns null on a failed creation (the original faulted).

## Registry files

The game's binary registry files (*.reg: quests, item databases, settings), read through `FileRegistry`. All offsets
from the start of the file:

    +0x00 u32 magic 0x45AB3EFB
    +0x04 u16 section count = size of the section hash table
    +0x06 the section table, 6 bytes per slot: u16 next slot in the chain (0xFFFF ends it), u32 offset of the section

A section: u16 key count (its hash table size), u16 name length, the name (no terminator), then its key table (6-byte
slots like the section table, offsets from the section). A key: u8 type, u16 name length, the name, then the value.
Types: 0 int, 1 float, 2 string (u16 length + characters), 0x80 / 0x81 / 0x82 arrays of them (u16 count first). A name's
home slot is (sum of tolower(c) over its signed characters) % table size; names compare case-insensitively over their
stored length. The server world's message texts are a FileRegistry (`WorldServer::Messages()`). `IniFile` reads .ini
files (integer lists too); the game's own options are in the settings file (settings.h, `docs/settings.md`).
Since MF3 `FileRegistry` holds the parsed sections (not the image), so a registry's TOML (`<name>.reg.toml`) merges
over it by section and key (mods.md, "Records as TOML"); the getters answer as before.

## Trader stock

`ItemStock` lists on a class template: "allowed" and "magic", with base and scroll levels. The camp's Create generates
only when the camp has a constructor record ("constr.current" > 0) whose flag is set (by the template's constructor and
again by quest completion); the flag is cleared after generating. A base's dialogue answer "constrN" sets
"constr.current" = N and asks for screen 5; the harness step `screen 5 <N>` does the same. A new game has templates 1,
3, 4, 5 ("b.smith.constr.1", "b.Shopper.constr.3", "b.kuzn.constr.4", "b.golem.constr.5"). GenerateItems (on "allowed")
copies the magic, resets the material units, runs the item generators, their instructions (1, 3) and the materials; on
"magic" (when the template's flag is set): GenerateScrolls (40, 50), GenerateSpellInstructions (1, 3) and
GenerateModifierInstructions (20, 50) into a list handed to the party.

Non-obvious: the count is min + (rand() * range / 0x7FFF) % range, and more prototypes than that shuffles the template's
prototype vector itself; a material candidate is drawn with one rand() against quality q and price p: 10% when q < 2p/7 -
p/5 or q > p/5, else trunc(110 - |(q - p/7) / (p/7 - p/5)| x 100)%; instruction items get their record's index;
FillSpellMaps keeps one entry per modifier kind letter a, d, e, m, r, t (replacing a held one unless at least as strong;
less or unordered replaces) and f / i under both letters; GenerateSpells keeps the level reached in a register (the stored
level is not updated); LoadLists reuses its count and three material ints for every section (a failed read keeps the
previous value). An item whose build fails is left out (the original used the null item). Kept: the by-name lookups insert a null entry for every
unknown name; a material entry with an unknown material is loaded with a null record. SaveLists writes a spell's code as
its first 4 bytes (the original added a stack byte after a 4-letter code).

## Party

`Party`: item manager, item constructor, spell manager, spell constructor, unit records, items, variables
(hash_map<string, float>), zone name. Create attaches both managers to the party, so the party is the context of its
items (an item's Load finds the party's spell manager). The party being loaded is `LoadingParty`.

### Party records and the party object

A constructed `PartyUnitRecord` has seven empty strings, empty lists, the two body ratios 0.8 / 0.2, height 1.0 and the
rest at their defaults (the destructor sets the same). `FillFromPrototype` takes the lists straight from the prototype;
`Assign` is shallow (item list pointers copied); `EquipFromPrototype` (a new multiplayer hero) releases spells and
weapons, then takes the NPC record's start spells, quick items, weapons, money and experience; `OpenEdgeExits` (on an
edge zone the exits' targets get "z.<name>" = 1 when 0 / NaN). Multiplayer hero records: `<folder><n>.mp` (0x74 bytes,
magic 0x114; a name that is not digits before its last '.' is deleted instead; the old file is moved to temp.mp around
the write); SaveAll empties the folder and writes every record. Kept: Destroy deletes the four objects whatever their
count; AddMember copies the stack record's uninitialised statistics (BuildNpcStats overwrites them). `SaveParty` draws
an ISAAC key.

### Party item lists

`Party::AddItem` merges with a held item that is the same (IsSameAs), else the party makes a copy (the factory makes the
item, the source copies itself into it); `RemoveQuestItems` (every new game) keeps all but quest items; `ReleaseItems(items)`
(multiplayer exchange). The record lists (weapons, quick items, armour, spells) add clones made with the world's spell
manager. `CarriedWeight` sums weapons, quick items, spells and armour in one running float.

## HUD arc points

The HUD regions' arc vertices are `_ftol(cos(k x pi x step) x radius)` with every product rounded to float but not the
cosine or sine themselves (the original's 24-bit precision reached FADD / FSUB / FMUL / FDIV / FSQRT only): sin(pi / 2)
x 34 rounds to 34, cos(pi / 3) x 34 = 16.9999991 rounds to 17. `OnArc` (ui_hud_corner.cpp) rounds each product with
RoundToFloat. The map's outline is `(_ftol(700 - sin x 65), _ftol(cos x 65 + 80))` the same way.
