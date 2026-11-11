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
record it is; its behaviour (`SpellRecord::behaviour`, `Spell::Behaviour()`) says what a cast does. The load
(`SpellDatabase::Read`, `SetBehaviours`) gives the game's 42 records the behaviour of their place, so for them the two
are one number. A record with a `script` gets `SpellType::Scripted` (any place; "Scripted spells"), any other record
after them (a mod's: the showcase's "spark", 42) `SpellType::None`, which starts nothing.

- What reads the behaviour: `SpellEffect::Start`, `UpdateLasting`, `DamageKindOf` (spell_effect*.cpp) and the cut
  spells in `CastSpell`. The healing item triggers (combat_actions `TriggerItemSpells`, combat_update `TickItemSpells`:
  armour casts it when not hit, the periodic cast only on a hurt wearer, one a tick) read the record's TOML
  `item_heals` (`SpellRecord::itemHeals`; the game's healing has it in assets/spells/spells.sdb.toml).
- **What an active effect does** (`MapEffect::Behaviour()`, the record's `activeEffect`, `itemdb::ActiveEffectAt`):
  the kind its TOML `effect` names (`SpellRecord::effect`, spell_db.cpp's `kEffectKinds`: the codes of the game's 20
  buffs and `teleport`, the stun without a look its caster gets; `none`: nothing), else (`own`, the default) its
  behaviour, so a game record still in code does what it did and a Scripted record without it nothing. The buffs'
  records name their own (`effect = "slow"`); a mod's buff names any. What reads it: the AI's `HasEffect` and the
  client's looks and sounds (`MergesParticles`, `ParticleSourceType`, `SoundFolder`, the rings, the invisible
  and stunned flags, map_object_client_effects.cpp; the stench and feeble mind sounds, map_object_companion.cpp). A
  kind without a look (None, Scripted) gets no particle source and no sound. The HUD's buff icons keep the record (its
  icon). A save's active effects hold the record's place, so they read the same whatever its `effect` says. What an
  effect does to the stats is its kind's OnStats ("Active effects on scripts").
- What the tables say of a spell is `itemdb::UseOf(record)` (`Spell::Use()`, spell_behaviour.cpp): the AI's option
  kind (`ai`), the damage type of the AI's estimate (`GetSpellDamage`), `offensive` (the HUD's use at once aims at the
  nearest enemy), `turnsVictim` (`CreateMissile`: the victim sees the caster, diplomacy counts an attack) and
  `fighting` (`UnitServer::IsFighting`: the battle music), and `effect`, the behaviour the AI's curse, protect and
  enhance options look for on a unit (`HasEffect`: "has it already"; the record's `aiEffect`). The game's spells still
  in code have one row each (`kGameSpells`) and look for their own behaviour; those on scripts have an empty row
  (Rebirth's FixSpellSelfCasting is in their records' `offensive`, FixPlaingBattleMusicFromAntimagic in antimagic's
  `fighting`). A Scripted or None record reads its TOML
  fields `ai`, `damage_type`, `offensive`, `turns_victim` and `fighting` (the last two as `offensive` when not given:
  -1 in the record) and `ai_effect`: the code of the spell whose effect it gives (a scripted spell applying the game's
  slow says `ai_effect = "slow"`), resolved by the load (`SetBehaviours`) to that record's `activeEffect`; without it,
  or naming no spell (a load warning), its own `activeEffect` (a buff's `effect`). The fields of a game record still
  in code are not read: its row is its behaviour's.
- `cast_school` (`SpellRecord::castSchool`, None: the record's own school): the school whose casting look the caster
  shows (`FillCastKind` → `OrderPlan::castKind`). The original casts Rick magic (a fire arrow) with lightning's look and
  curse magic (lightning) with fire's, perhaps a swap; assets/spells/spells.sdb.toml keeps it.
- Limits: a spell's place is a byte (prototypeIndex, MapEffect::type, saved and sent), a modifier's place + 1 a signed
  byte (`Spell::modifiers`); the load leaves out spells past 256 and modifiers past 127 with a warning.
- Checked by ei_unit_check's spell behaviour cases (tests/unit/spell_behaviour.cpp: a fake 300-record table cut to
  256, the game's rows whatever the fields say, a new record's fields, a script making any record Scripted,
  turns_victim and fighting of their own, ai_effect resolved, unknown or empty, `effect` named or the behaviour's and
  ai_effect following it) and by the goldens.

## The game's spells on scripts

The game's own spells move onto the scripted path family by family (MF17g; user 2026-11-11: the original content uses
what modders get), so the code they ran and the switches and table rows that keyed on them go. On it now: the
lightning arc and curse magic (assets/spells/Lightning.lua), the missiles: fire arrow, Rick magic and acid ray
(Missile.lua), the fireball (Fireball.lua), the area strikes: light strike and acid column (Strike.lua), the lasting
areas: fire wall, light wall and poison cloud (LastingArea.lua), the 20 buffs (Buff.lua), healing (Healing.lua),
teleport (Teleport.lua), clairvoyance (Clairvoyance.lua), fireworks (Fireworks.lua), vision fog (VisionFog.lua) and
the campfire (Campfire.lua). Still in code: possession, link and charm (cut from the game, unreached: only a save's
cast of one starts them).

- **Built in.** `assets/spells/` holds the scripts (CamelCase, by the spell they run) and `spells.sdb.toml`, the
  fields the game's records get: `script`, the use fields (`ai`, `damage_type`, `offensive`, `turns_victim`,
  `fighting`), `cast_school`, `per_target`, `ends_with_target`, `max_ticks`. The build writes the folder into the exe
  (src/engine/particles/embed_effects.cmake, `spell_files/builtin.h`). `SpellDatabase::MergeBuiltin` merges the TOML
  into the records of its codes right after the binary spells.sdb is read (never adding a record; a database without the
  code gets nothing), below every TOML layer, so a mod's spells.sdb.toml overrides any field. A whole-file TOML
  (`replace = true`, ei-convert's dump) stands alone: the dump carries the fields, since ei-convert reads the database
  the same way. The scripts are the bottom layer of `Res\spells\` (`ScriptSpellHost`'s ReadFromLayers falls back to
  `itemdb::BuiltinSpellFile`); a mod's `Res\spells\Lightning.lua` replaces the game's for both spells.
- **What keeps them the same.** `per_target`: CastSpell casts each one as before, once at every target it picks
  (t runes, f-rune filters, sight). `ends_with_target`: the cast goes at once when its target leaves the world, as the
  game's spells do (`Forget`). The cap does not cut them: a cast paced by timers alone lives up to 600 ticks or its
  duration (the game's durations of 1 are target counts); the lasting areas, which outlast their duration (up to 2550
  ticks with eight d runes, then 80 more), have `max_ticks = 0`, no limit. Their scripts do in order what the C++ did: the same
  objects made in the same order (their ids and the timed objects' order are state), the same hits, sounds at the
  same points, and the same ticks (the bolt's lifetime, a timer for the light's rise and one for the end, at the
  update the C++ timer reached them). Lua numbers are doubles: a script passes the cast's floats on as they are or
  applies one operation (exact once rounded back), never a chain; a float constant is written as the float's exact
  value (Missile.lua's 0.4 and 0.7 as `0.4000000059604645`, `0.699999988079071`: the product of two floats is exact
  in a double, so it rounds back to the float product). A product compared with a float is rounded first
  (Fireball.lua's `Float`, `string.unpack("f", string.pack("f", x))`): the exact double could pass where the
  float did not.
- **The missiles** (Missile.lua): OnCast ends at once unless the target is a unit (`cast:IsUnit`), else the missile
  (its trail, `size` the power x 0.4 for the acid ray, the looping `circle.wav`), the start sound at the cast's start
  and an `Expire` timer at tick 152 (the C++'s count of 150 ran out at the 152nd update). OnHit: the damage, then
  (after it: a dead unit's height is lower) `cast:HitPoint` (the ground + `Unit::ModeSpeed`, the C++'s splash point),
  the splash attached to the target (power x 0.7, `free`: a one-shot of 60 ticks that outlives the cast, as the C++
  never held it), the end sound and a light of `lightRange` x 1.5 rising at `lightRise` x 1.5, and `Done` 36 ticks
  later. A missile that lands with its target gone finishes the cast.
- **The fireball** (Fireball.lua): OnCast the flight (`cast:Flight` from the cast's start to its point: the C++'s
  FlightTicks and ScaledToSpeed, speed 5 when the spell has none), the landing point (`cast:Ground`, the water's
  surface + 1), the ball (`FireBall` at the caster's eye, size power x 0.3, flying by the velocity to the landing
  point, `unit` the caster), the start sound (one, moving, when the speed x 1.25 rounded to a float passes the
  distance; else `SoundLoop`: start then circle) and a light moving with it. The C++ counted the flight's ticks less 2
  down: the ball goes at tick count + 1 (`Out`, a `Release`) and the blast at count + 2 (at least 1): the hit
  (`FindUnits` area, `unsorted`: the sector map's order; `Damage` fire, `split` the duration), the ball, its sound and
  light released (in that order, after the hit), the blast (`FireBlast`, size the larger of `areaPower` and the
  power, alpha the power, `param` the radius, free), the end sound, a light of `lightRange` x 1.5 rising at
  `lightRise` x 1.5, the caster's `Noise` (5, call delay 4), and `Done` 37 ticks later.
- **The area strikes** (Strike.lua): the light strike at the water's surface - 3 (`Ground`): a bolt from 30 above,
  width power x -7 for one tick, the free `LightningBlast` (size the radius, alpha the power), the start sound (volume
  and hearing 1000), a light 5 above rising at `lightRise`, the hit (lightning, `unsorted`, `split`); a second bolt of
  width -7 at tick 3 for one tick, `Done` at 9. The acid column at the cast's point: the free `Nuke`, the start sound,
  a rising light, the hit (acid, `area`) at tick 15, `Done` at 32.
- **The lasting areas** (LastingArea.lua): OnCast the fire wall (the `Tendril` at the point with `direction` the span,
  `cast:Across` from the caster's eye or the cast's start to the point, the radius long: WallSpan of WallDirection;
  the light the radius + `lightRange` at the ground + 1, then the start / circle `SoundLoop` there), the light wall (a
  bolt of width 0 between the point -/+ the span, each end on the water's surface + 1, then the light and the circle
  loop) or the cloud (the `PoisonFog` at the ground + 1, size the radius, then the loop, then the light), and `Block`
  (the walls along the span, the light wall along half its bolt's `b - a`, the cloud a disc of the radius). The C++'s
  count from the duration down, its first lasting update at tick 1 (`Schedule`): a pulse at each count divisible by 4
  down to 0 (`FindUnits` area, unsorted, `along` the wall's span or the bolt's `b - a`; `Damage` area, split by the
  duration, `noCredit` after the first pulse: the C++'s flags bit 0), `Out` at -1 (each spell's own order of the end
  sound, the light sent up, `Unblock` and the releases) and `Done` 80 ticks after.
- **What differs.** The `SpellEffect` record of a live cast is the scripted one (state 4, no visual / sound / light,
  its objects in `owned`; the digest's missile group, a save's projectile record) and its entry is in the Lua state's
  `casts` until it ends. The world, the units and the timed objects are the same. The saved `nextCast` is the key
  after the live casts' (1 with none), so a save with no cast going is the same as before the port. The casts outlive
  a script reset (lua-scripting.md, "Scheduler"): the campfires a base's objects cast before its scripts load go on.
- **Old saves.** A save's cast of a ported spell in the C++'s states (1, 2) goes to its script's `OnResume(cast, old)`
  (`SpellEffect::ResumeScripted`, `ScriptSpellHost::Resume`, after the Lua state loaded): the cast gets an entry, the
  C++'s visual, sound and light become its owned objects (an EffectArrow a missile with a nil payload, so OnHit runs
  when it lands), and the hook, with the world's bindings off, sets timers from `old.state` ("missile" / "lasting")
  and `old.ticks` (the C++'s timer). Missile.lua's resumes a flying arrow (its hit lands) and a rising light at the
  update the C++ would have ended them. `old.visual`, `old.sound` and `old.light` are the handles of the objects the
  C++ held: Fireball.lua's resume of a ball in flight releases them at its blast (at the count + 2, the ball at + 1),
  Strike.lua's drops the light strike's bolt at tick 1, strikes its second bolt at its count's 5 and the column's hit
  at its 15. `old.direction` is the old visual's (a wall effect's extent, a bolt's `to - from`) and `Block` works in
  OnResume (the C++'s load stamped the footprint; the AI map is not saved): LastingArea.lua's blocks a standing wall or
  cloud again, takes the credit flag from the count (a pulse came when the count is below the duration's highest
  multiple of 4) and schedules from `old.ticks` as OnCast does from the duration. A script without OnResume (Lightning.lua: its hit was dealt at the cast) leaves the cast as it was, and
  it ends at its next update (Update's state 1 and UpdateLasting's default) with its objects. The save version stays
  0x74.
- **Cost.** A lightning cast is a few Lua calls at the start, a timer call at ticks 8 and 89 and the host's table walk
  on the other ticks. Measured (2026-11-11, `--quiet`, a scratch scenario of 600 casts at the hero, ten a frame, up
  to some 180 alive): the world updates took 0.34 ms a frame on the C++ spells and 0.47 ms on the script, about 24 us
  per cast over its 90 ticks.
- **The buffs** (Buff.lua: protection from fire, lightning and acid, eagle sight, infravision, detect life,
  invisibility, silence, lichdom, stench, stun, antimagic, strengthen, weaken, regeneration, feeblemind, speed, slow,
  enlarge, shrink): OnCast puts `cast:Apply(target, "own")` on the target (the C++'s ApplyBuff: `BuffRecord`, the
  spell's record with power x 16 and the cast's silentEnd as its end flag) and finishes, in the update the C++'s Start
  ran, so the cast ends at the same tick; no target (gone or none): nothing. What the effect does is the record's
  `effect` (its own code: "Spell behaviours"), so the stat effects, looks and the AI's "has it already" are as before.
  Their use rows are their records' fields (`ai`: protect, enhance, curse or none; offensive / turns_victim; none
  fighting, antimagic's by Rebirth's FixPlaingBattleMusicFromAntimagic). Old saves: an effect on a unit is its
  record's place and numbers (MapEffect, unchanged), read the same; a buff cast never lives past its first update, so
  no save holds one in the C++'s states.
- **The rest** (each on a file of its own; OnTick paces the four that act every tick, so their records have
  `max_ticks = 0` and the script ends them at the C++'s update; all have OnResume for a save's cast in the C++'s
  lasting state, from `old.ticks`: the C++'s count, the same arithmetic as OnCast's from the duration):
  - Healing.lua: OnCast ends at once unless the target is a unit; `Heal` by the damage, the free `Healing` glow attached
    to it (size the power), then, unless `cast.silent` (an item's periodic cast, the C++'s silentEnd) and not `IsHurt`
    (the packed hit points at their maximum, the C++'s full test), the start sound at the unit and a light at the
    point of the range negated rising at `lightRise`; `Done` at tick 32 (the C++'s count of 30).
  - Teleport.lua: the departure sparks at the caster's `Position` (or the cast's start) on the ground + its own z, held;
    the free inverted ones (size -1) at the point's ground - 0.2 (the float's exact value), the start sound and the
    light there; OnTick ends the cast at the stun (the duration, at least 1) + 32 or when the caster is gone or
    `IsDead`, and at tick 30 `Move`s it (Move's check, at its own height, `ResyncWithUnit`; the C++ resynced after the
    release and the effect, which reads none of them) or `Shout`s cant_teleport; arrived, the sparks released, its own
    effect (`effect = "teleport"`, power 16: the C++'s EffectRecord, not BuffRecord's power x 16) and the light sent up.
  - Clairvoyance.lua: the free `ClayRing` at the point (size the radius), the light at the ground + 1 (range the
    radius) and the start sound; OnTick with a caster `Reveal`s every unit in the radius (`FindUnits` unsorted: the
    caster's player's possessedGroup, emptied each tick) and `Wake`s those within the radius + 5, then at the count's
    10 the sinking ring (size -radius) and the end sound, and the end below 0 or without a caster.
  - Fireworks.lua: the `Geyser` at the ground + 1 (size the power), the light and the `SoundLoop` of the five numbered
    bangs; OnTick while the count is at least 0 `Wake`s (radius + 5) and has every unit in the radius `Notice` the
    point at the C++'s FrightLevel (each float step rounded with `Float`, then truncated); at -1 the end sound, the
    geyser and the bangs released and the light sent up; the end below -80.
  - VisionFog.lua: OnCast finishes without a caster or a target, else `Vision` (the EffectShowVisibility: the target's
    sight with the caster's senses, held) and the start sound at the target's eye; OnTick ends it at the duration + 2
    or when the target is `IsDead` (gone: ends_with_target).
  - The C++'s spell effect kept its point in `position` (the ground + 1 for fireworks and clairvoyance): their
    OnResume read it as `cast.from`. Their use rows are their records' fields (healing `ai = "heal"`, the others
    none; none offensive or fighting).
- **The campfire** (Campfire.lua): every flame a zone places on the ground (world_units.cpp `SpecKind::Flame`, its
  height below 0.7) casts one with no caster (CreateSpellFromText "campfire"), so every zone has some (gz1g two,
  Lost in Astral's gz10g 31). No objects, no footprint: the torch is its look. OnCast keeps the point's ground + 1
  (`Ground`, the C++'s position) and schedules from the duration: the C++'s count went up from it for ever, a hit at
  each count divisible by 4, its first lasting update at tick 1, so the first `Pulse` at `-count % 4 + 1` and then
  every 4 ticks (`max_ticks = 0`: it never ends). The pulse: `FindUnits` at that point, the radius, area, unsorted;
  `Damage` fire, area, split by the duration, the credit every pulse (no caster; the C++ never set flags bit 0).
  OnResume: the point is `cast.from` (the C++'s position), the schedule from `old.ticks` (the C++'s count at the
  next update). Its use row is its record's fields (none, not offensive or fighting, no damage type: the C++ row's).
  Cost: a timer call every 4 ticks; the ticks between touch no Lua ("Scripted spells", the idle ticks). Measured
  (2026-11-11, `--quiet`, `EI_PROFILE=1`, a scratch scenario standing 1000 frames in Lost in Astral's gz10g, 31
  campfires; the C++ campfire took 0.1%): `SpellEffect::Update` took 3.6-3.9% of the main thread while the host walked
  the entry on every tick (MF17g4a), 2.9-3.6% with the idle ticks skipped (MF17g4b), 1.5-1.6% with the hooked ticks
  cut (MF17g4c: "Scripted spells", the hooked ticks; Campfire.lua keeps its filter in a constant and makes the hit
  only when FindUnits found someone). Timed per tick in the same run (a scratch build): a pulse 4.9 us, of it 3.1 us
  the hook's plumbing around the Lua call, before MF17g4c; 2.9 us after (the plumbing about 1.0 us, the call 1.8:
  FindUnits 0.7-0.9, After 0.6, the Lua of Pulse 0.3); a tick between 0.08 us. The Lua state's collector also does
  less (luaC_step 1.1% to 0.2% of the stretch): the kept cast value made the garbage of every hook.
- The rest: `spells`, `combat`, `units`, `world`, `particles` and the other healing casts, `unit_ai_heal` (the AI's),
  `spells_more` (fireworks by script, the hero's teleport), `spells_cast` (clairvoyance and vision fog without a
  caster), `spells_hero` (the hero's vision fog), and `spells_old_rest` (tests/saves/rest_old, made by the C++ build:
  clairvoyance and vision fog lasting, a fireworks gone out and one frightening, a teleport 9 ticks before the hero
  arrives, a healing's light; all resume and end at their counts, the hero arrives; against the C++ build's run of it
  only the missile records differ); `--lua-check` (Position, IsDead, IsHurt, silent, Shout, Reveal, Wake, Notice,
  Vision's arguments).
- The campfire: every golden (every zone has some; the base and network zones' too, which load their scripts after
  their objects: the casts carry over that reset; only the missile records and the saves' missile records differ),
  and `spells_old_campfire` (tests/saves/campfire_old, made by the C++ build: four campfires at counts in two phases,
  two with blocked WolfTutor wolves in them; all resume, the wolves are burnt and die on the same ticks; against the
  C++ build's run of it only the missile records differ).
- Checked by `spells`, `combat_skills` and the other lightning and missile scenarios (the digest's missile records
  differ, all else the same), `spells_old_save` (tests/saves/lightning_old, made by the C++ build: both casts end at
  the load), `spells_old_missile` (tests/saves/missile_old, made by the C++ build: an acid ray lasting, a fire arrow in
  flight; both resume and the arrow kills its wolf; against the C++ build's run of it only the missile records
  differ), `spells_old_area` (tests/saves/area_old, made by the C++ build: two fireballs in flight, one with its sound
  sequence, an acid column before its hit and a light strike with its first bolt; all resume, the column and the far
  fireball kill their wolves; against the C++ build's run only the missile records differ), the fireball and strike
  scenarios (`spells`, `spells_cast`, `unit_ai_cast` and `unit_cast_at`: the AI's fireball, `unit_ai_corpse`: its
  noise), `spells_old_lasting` (tests/saves/lasting_old, made by the C++ build: a cloud gone out, two walls and a cloud
  standing; all resume with their footprints, wolves added in the walls and the cloud die at their next pulses;
  against the C++ build's run only the missile records differ), the lasting-area scenarios (`spells`, `spells_cast`,
  `save_flight`: a wall of 750 ticks saved and loaded mid-cast, its footprint stamped again) and `--lua-check` (handles, SetVelocity, Eye, a demon bolt, the timer-paced cap, IsUnit, HitPoint, a free
  effect, OnResume of a C++ missile with bindings off and its hit and handles; Ground, Flight, SoundLoop, Release,
  Noise, the Sound / Light / Effect options, FindUnits unsorted, Damage split, areaPower; Across, FindUnits along,
  Effect direction, Block's arguments, Unblock, Block let through in OnResume). The buffs: `spells`,
  `map_objects`, `rebirth_fixes`, `particles` and the AI's protect / curse casts, and `spells_old_buff`
  (tests/saves/buff_old, made by the C++ build: shrink, speed, protection from fire, invisibility and strengthen on
  the hero, stun, slow, weaken and feeblemind on a wolf; against the C++ build's run of it the same).

## Active effects on scripts

An active effect's stats come from Lua (MF17h1; user 2026-11-11: Lua-first, the game's buffs on the modders' path). A
record's `effectKind` (SetBehaviours) is the place of the record whose script holds the hook: the game's spell its
`effect` names (the game's records sit at the place of their behaviour), itself for `own`, none for `none`. The
twenty buffs' hook is Buff.lua's OnStats (a table by `effect.kind`), teleport's stun Teleport.lua's.

- **The totals** (src/game/spells/effect_stats.h, `EffectStat`, names in `kEffectStatNames`; the table in
  docs/modding.md, "Active effects"): numbers from 0 that the C++ applies where the original applied its strongest
  values. `UnitStats::Recompute` adds the three protections, sight, day sight, near sense, the four conspicuities (the
  sight, hearing and near ones then at least 0, as the original's subtraction was) and the haste to the rate (at least
  1), every total added even at 0 (the original always added); `EffectScale` (hit points x 0.005, damage x 0.003)
  reads Might and Frailty; `Regenerate` multiplies by Regeneration above 0; `RecomputeScale` reads Girth and Size (the
  original's integer levels, as floats); `AddEffect` stuns while Stunned is above 0, `EffectRemoved` unstuns when it
  is not; `UnitServer::EffectsChanged` sets MapObjectFlag::NoCasting from NoCasting. "Scales" (AddEffect and the
  removals: RecomputeHitPoints and RecomputeScale) is whether the effect's kind touched Might, Frailty, Girth or Size
  (`ScalesWith`, asked before a removal).
- **Bit-exact.** Each kind runs once with its strongest value (MaxKeepB from 0, the original's fold), by ascending
  kind; each stats call is one float operation with its argument rounded to a float. The protections: `Larger` of
  the protection's value and antimagic's from 0 is the original's `own >= antimagic ? own : antimagic`; Haste is
  `0 + speed - slow`; invisibility's `value * 0.01f` is exact in a double and rounds back to the float product, and
  `c + (0 - t)` is `c - t`. A NaN value differs only for a protection without antimagic (the original's select took
  0, Larger takes the NaN); no spell's damage is NaN.
- **Where it runs.** `MapObject::EffectTotals` (map_object.cpp): a server object's `MapObjectServer::effectStats`
  (`ActiveEffectStats::Of`): the kinds and strongest values worked out from the list each time (cheap), the Lua only
  when they changed (bitwise), in the server world's main script's state (`ScriptSpellHost::EffectTotals`, the world's
  bindings off, so the result depends on nothing else). The client has no effect values (the network sends type,
  power, ticks and the end flag) and no server state: its totals are none, which is what its values of 0 gave the
  original's client (its only read, the attack display's damage scale, was 1).
- **Left on SpellType.** The client: the looks, sounds, Silence's mute and `Unit::CanCastSpell`'s feeblemind (the
  battle interface's spell cursor and caster choice) are MF17h2's; the AI's "has it already" (`HasEffect` of the
  record's `aiEffect`).
- **Cost.** Measured (2026-11-11, `--quiet`, a scratch scenario: ten GoblinF1 given buffs by script casts fighting
  the god-mode hero for some 1200 frames, a timer around `Of` and `Recompute` in a scratch build): 2450 `Of` calls,
  140 of them running the Lua (2.0 us each, three kinds), the others a cache hit (0.09 us); 0.49 ms in all over the
  fight. A recompute took 8 us on average with its totals in it.
- Checked by `--lua-check` (Buff.lua's and Teleport.lua's totals against the C++ formulas: protections with
  antimagic, invisibility, speed less slow, the steps and the stun, what each kind touched; a test file's float
  operations in order, a failing hook adding nothing, an unknown stat, a world binding, a missing file, the stats
  value outside its hook) and the goldens that reach the buffs (`spells`, `map_objects`, `rebirth_fixes`,
  `particles`, the AI's protect / curse casts, `spells_old_buff`).

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
- **The cast value**, one table per live cast (its clock's registry reference, made at its first hook, dropped with
  the clock; not in the entry, so not saved): before each hook PushCast sets again only the fields whose value
  changed (floats by their bits; a moved point a new table, so a point a script kept stays as it was; an object by
  its pointer: Forget makes a gone one nil) and the data every time. The game's scripts only read them; a field a
  script sets on it stays until the cast's own value changes and is not saved. Its fields: `id` (the key), `code`,
  `caster`, `target` (objects or nil), `from`, `point` (`{x, y, z}`), `tick` (ticks since the cast; 0 in OnCast), the built `damage`, `range`, `radius`,
  `duration`, `targetCount`, `speed`, `mana`, `power` (`spellfx::Power`), `areaPower` (`spellfx::AreaCurve` of the
  radius over the record's area), `lightRise` (`spellfx::LightRise`),
  `lightRange` (the record's), `school` (recordnames::kSpellSchools), `silent` (the effect's silentEnd: an item's
  periodic cast), and
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
  CreateParticleSource table then the data effects; `free`: only into the timed objects, as CreateParticle without
  keep), `Sound` (`CreateSound`), `Light` (`NewPointLight`), `Eye` (`EyePoint`), `HitPoint` (`GridHeightAt` +
  `ModeSpeed`), `IsUnit`, `Ground` (`GridHeightAt`, or `RawSurfaceHeightAt` with `water`), `Flight` (`FlightTicks`,
  `ScaledToSpeed`), `SoundLoop` (`NewSoundSequence`, CreateSoundSequence's object), `Release` (the slot's release:
  `RemoveTimedObject`), `Noise` (`aiMap.events.Noise` from the caster's controller), `Across` (`WallSpan` of `WallDirection`), `Block` /
  `Unblock` (`SpellEffect::Block`: the spells' footprint set `AlongDirection` / `DiscAt`, `StampFootprint` at the
  world's tick, `ClearFootprint`; one per cast, cleared at its end and in the effect's destructor; kept in the entry's
  `block`, stamped again by Adopt), `Position` (`localPosition`), `IsDead` (`Unit::IsDead`), `IsHurt` (the packed hit
  points against their maximum), `Shout` (`UnitServer::Shout`, the kind by `acks::KindByName`), `Reveal` (the caster's
  player's `possessedGroup`), `Wake` (`WakeUnitsAround`), `Notice` (`brain.memory.Notice`, decay -1), `Vision` (an
  `EffectShowVisibility` of the seer and the observer, owned); FindUnits' `unsorted` keeps the sector map's order (HitUnitsInRadius'), its `along`
  is the walls' plane test (HitUnitsAlong's: the normal 2 / sqrt(x^2 + y^2) long, |d . n| <= 1), Damage's `split` is
  `SpellEffect::SplitHit` (GetDamage's 4/n), Effect's `param`, `target`, `unit` and `direction` (EffectWall's
  SetDirection, only for Tendril and Link) set the particle's typeParam, target, unitId and extent before it goes into
  the world (what the C++'s SetColors / SetTarget / unitId / SetDirection after it did; AddTimedObject marks it changed
  either way). Arguments are
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
  ticks left, no missile in flight and no OnTick: "idle"; the record's `max_ticks` replaces the cap, 0 none); a hook's Lua error goes to the console (`Script error: <file>:<line>: <message>`), then OnEnd(cast,
  "error"). The effect then ends as any spell does (state 3, removed at its next update).
- **Idle ticks** (MF17g4b): the host keeps each live cast's `Clock` in C++ (its tick count, its earliest timer's due
  tick, its module's registry reference), filled by Settle after every hook. A tick with no missile arrived, no timer
  due, no OnTick in the module (a plain table, looked up there each tick: a hook may add one), below the cap and
  something pending (a timer or an owned object) only counts the tick and the owned objects' lives in C++: nothing
  else would have run. The entry's `tick` is written back before any Lua reads it (the next hooked tick, `After` on
  that cast from another's hook, the host's Save and so a script reset's carry-over); `After` lowers the clock's due
  tick, `Finish` and a restore make the clock unknown, so that the next tick takes the whole walk. `--lua-check`'s
  waiter / poker casts check another cast's After and Finish between a cast's idle ticks; the saved-at-every-tick
  pulse checks the write-back.
- **Hooked ticks** (MF17g4c): a known clock also stands for the entry between hooks. Its `due` is the earliest timer
  left: the timers' walk sets it to the second lowest due when it takes one (the one it takes is the lowest), After
  lowers it, so the walk runs only while a timer is due (once for one) and Settle reads the tick and due from the
  clock instead of the entry; `finished` is read only from an unknown clock's entry (Finish makes it unknown). The
  module comes from the clock's reference (looked up by file once per cast), the cast value is the kept one, and a
  timer's name stays a Lua string on the stack while it runs. `--lua-check`'s kept cast checks the kept value (the
  same table, a moved point a new one, a forgotten target nil).
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
- **In the game** a Scripted record goes the game's spells' ways, with nothing of its own: the AI's options by UseOf
  (`ai`, `damage_type` for the estimate, `ai_effect`; units with script names get no spell options, as for any spell),
  the HUD's column, keys and targeting by `target_kind` and its use at once by `offensive`, the item pictures
  `spell%04d` / `prototype%04d` by `icon` (a mod's `Res\textures\` PNG), and the ways to get it: `GiveUnitSpell` (a
  unit's own slots; a party unit reads its party record), `GiveItem("spell container[<code>]")` put on in the camp, the
  trader's magic stock by `sold_by` and a `[[scroll_recipe]]` (its scrolls and the spell maker's prototype loot item,
  `prototype.<code>`), and the camp's spell maker from that prototype. Casting pays the mana and plays the cast
  animation as for any spell.
- **Mods in multiplayer**: only the server runs the Lua; clients see its timed objects (a bolt, particles, lights) and
  the units' replicated state. `Res\spells\` counts in the mod's content hash (mods.md, "Mods in multiplayer"), so both
  peers have the same records and files, and an original client is refused by a server that plays a mod.
- **Checks in the game** (tools/make_spell_mod.py: "mf17d_bolt", a bolt and a blow through the game's own bindings
  CreateLightning and InflictDamage, 60 ticks): `mod_spell_ai` (zone1's wolf 1804 given the spell casts it at the
  hero by its ai = "attack"), `mod_spell_hero` (Ingos' stock has its scroll and prototype, `campstock`; the camp draws
  the mod's pictures, `read mod`; the container put on, the hero casts it at once, Ctrl and its key, at a spawned
  wolf made an enemy by SetPlayer (world.md: AddUnitToServer's units are on the hero's side); the client has the
  bolt, `clientbolts`), `net_pair_mod_spell` (make_mod_pair.py spell, its blow 20: the host's server casts it, the peer's
  client has the bolt, `clientbolts`, and the target below its maximum hit points, `clienthurt`; a blow of 5 left
  the hero's as they were).
- **Checks.** `--lua-check`'s host cases (tools/convert/lua_check.cpp `CheckSpellHost`: timers in order with payloads,
  Finish, OnTick to the cap, idle, a hook's error, a wait in a hook, no bindings at the top level and the file said
  once, a timer naming no function, a cast saved after each of its first six ticks ending as the unbroken one; the
  world methods on a stone in a world without a map: their arguments and errors, Apply / Has / Remove, what is owned
  and when it goes, OnHit with payloads, the idle end); `mod_spell_script` (both installs; tools/make_lua_mod.py
  spell): a console cast of a timer spell, saved and loaded mid-cast, its count going on to the same end tick as an
  unbroken run; `mod_spell_api` (both installs; make_lua_mod.py spellapi): zone1's wolf 1804 casts a spell that uses
  every world method on a spawned wolf, two missiles with OnHit and a data effect for 300 ticks, saved and loaded
  mid-cast, ending idle at tick 300; `mod_showcase_spell` (both installs): the showcase mod's Chain Spark
  (examples/showcase_mod's `Res\spells\chain_spark.lua`, mods.md "The showcase mod"), cast by the hero from the HUD
  at the golem: missiles with OnHit, FindUnits for the next victim, Damage, Apply (slow), Effect (its own data
  effects), Bolt and a timer per leap; three strikes 12 / 7.2 / 4.32 in game variables, saved after the first and
  loaded, the chain going on to the same strikes.

## Spell effects

`SpellEffect` (world.h calls it `Projectile`) is one cast spell and embeds its Spell. The world server ticks `Update`;
the state: 0 casting (`Start`, a switch on the prototype whose `Start*` functions set the state themselves: the
lasting one through `EnterLasting` or `kStateEnded`; the charm leaves it at 0 and so starts again every
tick), 1 a missile in flight (only a save's from before its spell moved onto a script: it ends at its next update),
2 lasting (`UpdateLasting`), 4 scripted, else it ends (a scripted cast's footprint cleared, the owned and the three
effect objects dropped). The three counted effect objects
(visual, sound, light) are also in the world's timed objects; replacing one removes and releases the old.

Spell behaviours: 0 / 0x28 fire arrow, 1 / 0x29 lightning, 2 acid ray, 3 fireball, 4 light strike, 5 acid column, 6 fire
wall, 7 light wall, 8 poison cloud (all eleven on scripts), 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26 buffs (on Buff.lua: the target gets a
record with byte 1 = trunc(power x 16)), 0x12 fireworks, 0x14 clairvoyance, 0x17 vision fog, 0x18 healing, 0x1C
teleport, 0x27 campfire (on their scripts), 0x15 possession, 0x1B link, 0x24 charm (cut, in code). `SpellType` names all 42 after the spell database's
prototypes (0x16 shapechange and 0x23 comprehend start nothing).

"Power": `pow(clamp(damage / prototype effect, 1, 8), 0.33) - 0.2`, or 1 when the prototype's effect compares equal to 0
(NaN included); the clamps let NaN through. The area power (the fireball blast's size, the cast's `areaPower`) uses
the same curve on `(r*r*pi) / prototype area`.

Lights: the prototype's fields 23-27 are its light (`lightColor`, `lightRange`, `lightRiseDivisor`); the lights rise at
`lightRange / max(lightRiseDivisor, 1)` a tick, the impact lights 1.5 times faster and further.

Hits: HitUnit skips units the spell may not affect (only checked with a caster) and calls the controller's TakeHit;
FindUnits `area` keeps the units that area spells hit (size class set, or the prototype's height below 0.5); the walls'
FindUnits `along` only those within 0.5 of the vertical plane along their direction.

Kept: healing and charm read the
target's class without a null check; charm with a
target or caster that is not a unit returns without changing state, so it restarts every tick; the light strike uses -7
as bolt width factor (Strike.lua); the light wall's footprint is half its bolt's `b - a` with the ends' heights in it
(its reach grows with their difference), the fire wall's the span.

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
