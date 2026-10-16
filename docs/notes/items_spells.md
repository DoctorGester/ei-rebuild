# Items, spells and related data (rules, texts, textures, registry, trader stock, party)

Layouts, record sizes, field offsets and vtable addresses are in the headers (`item_db.h`, `item.h`, `spell.h`,
`item_stock.h`, `party.h`, `texture.h`); this file holds what they do not say. Code in `src/game/`, wrappers in
`tests/verify/verify_<name>.cpp`; every rewritten function carries its address in the source (grep it), so this file
lists addresses only where they explain something. The HUD panel displays, portraits, top panel, objectives and save
screen pieces are in `ui.md` (sections 7 and 8); players and their message handlers in `objects.md`.

| Subsystem | Code | Scenario |
|---|---|---|
| Item rules | item, item_slots, item_lifetime, item_save | items |
| Item parts, constructor, names | item_parts, item_constructor, item_names, item_model_names | items, ui_camp |
| Item texts | item_db_text | items |
| Spell rules, construction | spell, spell_cast, spell_text | spells |
| Spell effects | spell_effect, spell_effect_start, spell_effect_update | spells, spells_cast |
| Quest database | quest_database, quest_records | any new game in zone1 |
| Textures (mmp, composed) | texture_mmp, texture_compose | all (skins at zone load) |
| Registry, ini | registry, ini_file, win_registry | all |
| Trader stock | item_stock | trader_stock |
| Party, party records, item lists | party_items, party_lifetime, party_save, party_members, party_file | ui_camp, trader_stock, party_records, items |

Stateful functions (create / release counted objects, deal damage, draw from ISAAC, write files) are verified "ours only"
(O) with invariants on the result; pure ones (P) or side by side (S). Jump tables (0068B700, 0068B770, 00683950, 006A0110,
006A4230, 006A5470, 0068BEC0) were read from the exe, not the decompiler. Callers' forwarders reach the rewrites through
their addresses, so wrappers run. The original's quirks (null reads, uninitialised stack bytes that ours zero, leaks) are
reproduced or noted in the functions' comments.

## Databases and naming

Eight STLport vectors of fixed-size records (007C86D8 materials, 007C86F8 weapons, 007C8718 armors, 007C8738 quick items and
spell containers, 007C8758 / 007C8778 quest and loot items, 007C8510 spells, 007C8530 spell modifiers). A lookup checks
`index < (end - begin) / stride` (signed division, unsigned compare), else null; callers then read fields of the null
record, so an index out of range faults (kept: `Field<T>()`).

**Naming (fixed in C2i):** the game treats 0x3007 as the loot item ("litem", item_loot, CItemLoot kind at +0x4C, table
007C8778, 0x78-byte records) and 0x3009 as the quest item ("questitem", item_quest, table 007C8758, 0x7C). `item.h` /
`item_db.h` used to name them the other way; they now follow the game (`kItemLoot` = 0x3007, `Loot()` = 007C8778, `kLootSize`
0x78; `kItemQuest` = 0x3009, `QuestItems()` = 007C8758, `kQuestItemSize` 0x7C). The file-local forwarders in item_db_load.cpp /
item_db_open.cpp and `ChunkArchive::SerializeLoot` / `SerializeQuestItems` keep the old sense.

Weapon stats as the unit stats code (00538960) combines them: min damage = weapon damage x material damage, max = (spread +
damage) x material damage, attack = weapon attack x material attack, damage type = first non-zero weight (6 if none).

Record values (item_db_records.cpp; O, every run: the vectors grow while the databases load): `ItemRecordHead::Assign`
006A8F20 copies three strings, +0x0C, +0x14..+0x1C, +0x34..+0x4C, byte +0x68 and `CStringArray::Copy` of +0x20 (+0x50,
+0x64, +0x6C not copied); the five kinds' uninitialized_copy / uninitialized_fill_n are `__fastcall`, RET 8, the second stack
argument an unused tag: weapons 006AB9C0 / 006ABB50 (vtable 00744068; own bytes +0x6C..+0x83, +0x8C..+0xB4), armour 006ABCD0
/ 006ABE20 (00744064; +0x6C..+0xB4), quick 006ABF60 / 006AC0D0 (0074405C; +0x70 and +0x80..+0x8A, the string +0x7C
constructed empty, the category not copied), the 0x7C kind 006AC240 / 006AC3B0 (00744058; +0x6C, its zone vector +0x70 left
empty), the 0x78 kind 006AC520 / 006AC640 (00744054; +0x6C..+0x77); the weapon record's operator= 006A8FB0
`WeaponRecord::AssignWeapon` skips +0x50..+0x67 (called by STLport inserts). The copies drop the fields listed as not
copied. The six record destructors 006A88F0..006A8BD0 are in library.csv.

Record Serialize (A-H23, item_db_serialize.cpp; loading read twice, ours then the original, digests compared; saving O):
`SerializeHead` (fields 0-15), weapon, armour (two 9-dword rows at +0x6C / +0x90 that straddle the declared members),
quick, the 0x7C kind (zone list through InlineStrings), the 0x78 kind (fields 0x15-0x17).

Loading (item_db_load.cpp; O; startup, every scenario): `IndexedRecordTable::LoadQuickItems / LoadQuestItems / LoadLoot`
(a stack archive: ConstructLocal, Open, the vector serialisers, Close; then each record filed under its name with
find_or_insert 00613B70 / 006A7870 / 0069E2A0, originals_library.h; Rebirth's patched 006A67A0 calls them by address),
`ChunkArchive::SerializeQuickItems / SerializeLoot / SerializeQuestItems` (the quest_records.cpp pattern; the loads call
them by address), `SerializeMaterials / Weapons / Armors`, `ItemDatabase::DestructBody` 006A6570 (007C86D8; six tables last
to first: index clear + buckets, or the materials' index destructor 006A6C20; on exit). Spell database (A-H21,
verify_spell_db_load): the spell, modifier and 0x34 list records' Serialize 00683B80 / 00683AF0 / 00683D40,
`ChunkArchive::IntVectorField` 00683EB0, the vectors 00684EF0 / 00684FE0 / 00684C50 (SpellRecord / SpellModifierRecord /
SpellListRecord in item_db.h).

## Item rules (scenario items)

Pure/getter slots (P): `CItem::GetPrototype/GetCategory`, `CItemWeapon::GetType`, `IsSameAs` (slot 14; base 0069FE90),
`GetTypeGroup`, `CItemLoot::GetPrice` (slot 16); the fastcall stat helpers WeaponType, MinDamage, MaxDamage, Attack,
DamageType, Stat84, Stat88, ArmorFieldB8 take a packed id / index (P with float bits). `GetDamageTypes`, `Recharge` (slot
6), `RollDamage` (declared `double`, unrounded ST0; ISAAC state restored between runs) are S. `InitFromRecord` (slot 13) is S
on a scratch copy; constructors are S on seeded scratch except the scroll's (O: a CALL within 5 bytes, no trampoline)
(`item_lifetime.cpp`); `CreateItem`/`DestroyItem` (slots 2 / 3) and the per-class destructors (slot 0, one byte-identical
copy per class, each storing CItem's vtable) are O. `Load`/`Save` (slots 9 / 10, `item_save.cpp`): Save S into capture
streams; Load O, then the original Save must write back the bytes read (every new game's autosave; world_load,
ui_saveload).

Load / Save: the base writes +0x18, +0x20..+0x3C, the spell's id (-1 none; Load looks it up in the context's spell manager),
+0x48, +0x40; armour adds its two absorption rows (not layerMask_). The loot's Load reads kind_ / recordType_ through the
stack slot of its stream argument, CItem's the spell id likewise (kept). Constructors leave +0x40 (CItem) and the quick,
quest, loot and scroll fields (+0x4C..) as they are; +0x20..+0x3C start at -1.0. The link at +0x1C is a CRefCounted (16-bit
count) that Destroy deletes at 0. IsSameAs compares the base floats (+0x20..+0x2C, +0x34..+0x3C) as "equal or unordered"
(FCOMP, TEST AH,40h) and the armour's rows and masks as dwords; the subclasses' slot 13 inline CItem::InitFromRecord and add
their record fields (the quest item takes the head's +0x6C).

## Item parts, constructor and names

`WeaponModelParts`, `AppendWeaponModelParts` (S into scratch), `CItem::AttachSpell`/`SetSpell` (O), `CItemConstructor`
(006A5B20-006A5D30: destructor body, Reset, Detach, SetManager, SetItem, Begin/EndCreateItem, Cancel, SetMaterial /
SetPrototype / SetSpellObject, Build; O), the per-class `ModelName / FirstListName / FigureName / PictureName` (slots 17-20;
S on text), `WeaponBaseFigure`, `ArmorModelName` (S), `CSpellManager::CreateSpellFromText` 00689030 (spell_text.cpp, O),
`CItemManager::MakeItem` 006AD000 (item_constructor.cpp; CItemManager derives CResourceManager, context slot 16 on
CManagerContext; O).

- Build's temporary record is built like the game's record classes (vtables 00744054..00744068); the extra members of the
  quick / quest / loot records leak as in the original. Build reads operands into locals before multiplying;
  `InitFromRecord` copies +0x40 / +0x48 through FLD / FSTP so a signalling NaN is quieted the same way.
- Name builders with an unknown weapon / armor type: the original formats an uninitialised 4-byte stack buffer, ours ""
  (no such types in the databases).
- `ArmorModelName` formats "%s_%.2d.%s.%d" (type code "hl","pl","lg","sh","pt","bt","gl"; type compared unsigned with 6). The
  original copies the code into the stack slot of its material-index argument and formats that slot, so a type without a code
  yields the index's own bytes (reproduced). Callers: unit skins 00536670 / 00568800 / 005685E0. `WeaponBaseFigure` is reached
  through the weapon's 006A0F50 from the unit effect point 00532860 (effects, spells_cast); its 4-byte number buffer would
  overrun above 999 (not in the database, not reproduced).

## Party item lists and CopyTo (O, counts and order checked)

`CItem::Clone` (0069FF70, RET 4, spell manager), `CopyTo` (slot 15's base body, RET 8; the per-class overrides return 0 for
another kind, else 1), `CParty::AddItem` 00661DB0 (RET 0xC; flags read as bytes) / `RemoveItem`, `RemoveQuestItems` (the
list minus class 0x14; every new game), `ReleaseAllItems` (multiplayer exchange only; callers keep their forwarders),
`PartyUnitRecord::AddWeapon / AddQuickItem / AddArmor / AddSpell` and the four Removes (lists +0x730 / +0x740 / +0x750 /
+0x760).

Clone = the owning manager's CreateObject(kind, -1) (slot 23), then the source's slot 15; on failure the new item gets a
reference and is released. CopyTo destroys the target (slot 3), gives it +0x18 and dwords +0x20..+0x40, a CSpell::Clone
(0068B410) of the attached spell or none, count 1; the overrides call the base through its address (`CItem::CallCopyTo`). The
record adds clone with the world's spell manager (CWorld slot 22, 0055EBA0: +0x4C). The party's adds merge with a held item
that is the same (slot 14), else the party's slot 24 (0055E940: the factory makes the item, the source copies itself into it)
gives the copy; array growth is AlignedPtrArray::SetSize (00519D70). Reached by ui_camp (0060FCA0 / 0060FE40 / 0060E900),
trader_stock (004A6F60 into 00661DB0), party_records (0058FD90 / 00590C70).

## Party records and the party object (party.h)

Verification: `PartyUnitRecord::Construct`, `ConstructPartyUnitRecords` (stdcall: zero, then construct each), CParty /
CItemConstructor / CSpellConstructor::Construct are S on seeded scratch (the party's variable map: bucket count and
emptiness); Destruct, ClearItems, `PartyMemberArray::SetSize` (CParty +0x2C, MFC CArray<PartyUnitRecord>, vtable 00742FF4),
`Serialize` (= `mfc::CArray::Serialize` with a minimal `mfc::CArchive`; vtable slot only, no caller), CParty
DestructBody/DeletingDestruct/CreateParty (2)/DestroyParty (3)/Clear (RET 8) and `CSpellManager::Construct` (an empty map of
193 buckets at +0xBC) are O. Record Load / Save, `LoadParty` / `SaveParty` (RET 8; the flag covers the variables; SaveParty
draws the ISAAC key for +0x50, restored between runs), `Assign`, `CopyConstruct` (`party_save.cpp`): saves S into a scratch
memory stream; record Load round-trips through Save; the rest O. `party_members.cpp` (O; party_records, ui_hero_screen
(00616AC0)): `AddMember`, `FillFromPrototype` (takes the lists straight from the prototype instead of through 005143E0's
CStringArrays: same names, same order), `Assign` (shallow: item list pointers copied), `CopyItemsFrom` (party slot 24 copies),
`RemoveMember`, `EquipFromPrototype` (a new multiplayer hero: spells / weapons released, then the NPC record's start spells,
quick items, weapons, money (+0x58), experience (+0x54)), the spell list's Add, `AlignedArray::RemoveRange` (only caller
006625B0), `OpenEdgeExits` (on an edge zone (kind 1) the exits' kind-0 targets get "z.<name>" = 1 when 0 / NaN; newgame login).
`party_file.cpp` (O; ui_hero_delete; SaveAll when the hero screen closes with heroes): `MpHeroRecord::WriteFile / ReadFile`
(0x74-byte hero records `<folder><n>.mp`, magic 0x114: a name that is not digits before its last '.' is deleted instead; the
old file is moved to temp.mp around the write), `SavedParties::SaveAll` (folder emptied by ClearSaveFolder, every record
written), `SavedParties::Destruct` (static destructor of 007C82A0 at exit).

A constructed record has seven empty strings, empty lists, +0x724 0.8, +0x728 0.2, height 1.0, +0x770 2, +0x774 0, +0x778 0,
+0x77C -1, +0x780 1 (the destructor sets the same). CParty (0x70): +0x1C item manager (0xBC, CResourceManager, vtable
0073F25C), +0x20 item constructor, +0x24 spell manager (0xD4), +0x28 spell constructor (getters: slots 18, 16, 22, 17), +0x2C
records, +0x40 items, +0x50, +0x54 hash_map<CString, float> variables, +0x68 zone name, +0x6C. Create attaches both managers
to the party (slot 13), so the party is the context of its items (CItem::LoadItem's slot 22 finds the party's spell manager).
Clear(1, 0) after Create re-attaches the spell manager. Global 007C8344 = the party being loaded (LoadingParty). StatsLoad /
StatsSave are in originals_units.h. DestroyParty drops the four objects by count and deletes them whatever the count;
AddMember assigns the stack record's uninitialised statistics to the new record (BuildNpcStats overwrites them).

## Item texts (004E0000-004E15C0; item_db_text, scenario items)

The range holds item names / descriptions from the text resources and the text manager's archive list. The item databases
are filled elsewhere (00456440 Rebirth-patched, 0057CB50, 0057DEE0). The "can't parse color/percentage" strings at 004A0xxx
belong to a 3DS material chunk reader (004A0BA0, 004A09E0), not item code.

**CTextManager** (`*(CTextManager**)0x007AF788`): `+0x1C` STLport list of text archives (node: next, prev, `+0x08`
stlp::string name, `+0x14` CMappedResource*), `+0x20` read cursor, `+0x24` start of the current text (null: none), `+0x28`
its size. A lookup asks every archive's GetFile (slot 5) in turn; the first hit becomes the current text. The line reader
(004DF8D0, 004DFB80) is `text_reader.cpp` (script.md).

**Text keys** (record name with ' ' replaced by '_' (004DC400), upper-cased; material = material record +0x00, others =
record +0x04): 0x3004 `weapon NAME MATERIAL` (kind text item_weapon), 0x3005 `armor NAME MATERIAL` (item_armor_upper for type
0-2, else item_armor_lower), 0x3006 `qitem NAME` for prototype +0x10 "none" (item_potion) else `qitem NAME MATERIAL` (category
5 only: item_wand, name + ": " + spell name, "Unknown" without text), 0x3007 by +0x4C: 0 `litem NAME` (table 007C8778,
item_loot), 1 `material MATERIAL` (+0x54, item_material), 2 by +0x50: 0-2 `instr NAME` (weapon / armor / quick record +0x54:
item_weapon_shape, item_armor_shape_upper / _lower, item_wand_shape), 3 `spell ` + spell record +0x04 CString
(item_prototype), 4 `modifier ` + modifier record +0x04 (inline code; item_modifier); 0x3008 `spell ` + its CSpell's prototype
+0x04 (item_spell); 0x3009 `questitem NAME` (table 007C8758, item_quest). The kind text is looked up as `string <kind key>`;
the display name is `format_item1` (kind, name) or `format_item2` (kind, name, count) when the item's +0x48 stack count is
above 1.

Functions: `IsPastEnd` (P), `GetString`, `GetItemTexts`, `FormatItemName` (S into scratch CStrings aliased like the real
outputs: 0063CEE0 and 0063E820 pass one CString as both description and kind), `TextSourceList::Begin`,
`TextSourceIterator::CopyTo/Erase/Insert` (O; node on top of the 0x18 free list 0079B22C, written directly). GetItemTexts
always returns 1 (FormatItemName's failure branch is dead); FormatItemName reuses the description string for the format
text; GetString on a missing text copies the key itself into `out` (shown as is in the UI); the text manager never closes
files from CMappedResource::GetFile (reference counts only grow, twice as fast in the verify build). Not rewritten:
004DEBC0, 004DC400, 004DE930 (add archive), 004DEAF0 (remove archive), 004E15C0 (node creation).

## Spells: rules and construction (scenario spells)

- CanAffect target flags: 1 = not hostile units only, 2 = hostile only, 4 = not humans ("unhuma"/"unhufe"), 8 = not
  "unmogo", 0x10 = not orcs ("unorma"/"unorfe"), 0x20 = not "unmoli".
- Damage types (`DamageInfo`, filled by `CSpellEffect::GetDamage` 00683950 and the weapon code 0054F630): spells
  0/3/6/0x27/0x28 deal type 3, 1/4/7/0x29 type 5, 2/5/8 type 4; a hit on several targets scales amount and multiplier by 4/n.
- Verification: `GetPrototype`, `GetModifierCount`, `GetDamageType` 0068B700, `IsOffensive` 0068B770 (AL), `HasCastFlagModifier`
  0068B7C0 (AL), `CanAffect` 0068B4C0, SpellCodeByName / SpellModifierCodeByName (stdcall) are P; `Clear`/`InitFromPrototype`/
  `ClearModifiers`/`CopyTo` S, `Clone` O; `GetDamage` S into a scratch DamageInfo; constructors S on scratch copies, destructors
  O; `CodeIndexedTable::FindByCode` 0068C820 / 0068C920 (the index's operator[], unknown codes filed with null; S: ours, then
  the original finds the same entry, skipped when its resize would rehash); `LoadSpell`/`SaveSpell` (0068B860 / 0068B920) and
  `MapEffect::Load/Save` (spell_save; +0x1C..+0x2F is a MapEffect, `CSpell::Effect()`) S (saves captured, loads replayed into
  the object's old copy; ui_saveload, world_load); `CLight::GetIntensityAt` 0068D730 (the original starts with a 4-byte CMP +
  JNZ, so the wrapper calls its two branches 0068D736 point light / 0068D7B4 other directly) P and `SetLitColors` S;
  `CSpellContainer` Reset/Shutdown/NameField/AddModifier/AllowsModifier/Build (Build S on bytes +0x48..+0x8C and the code
  string); `CSpellManager::NewContainer/CreateSpell`, `CSpellConstructor` Reset/Detach/SetManager/BeginConstruct/
  EndConstruct/AddModifier O.

**Cast chain.** `CastSpellUnit("healing{e2;e2}", ...)` and the other text casts go through CreateSpellFromText (00689030;
text before `{` is the prototype, `;`-separated codes are modifier names), then as CreateSpell 00689310 does with records:
the context's (manager +0xAC) slot 17 gives the CSpellConstructor; SetManager(this); BeginConstruct (a new container from
manager slot 23, class 0x3003); AddModifier per modifier (at most 8 in CreateSpell), accepted when AllowsModifier says so
(prototype flag bytes +0x78 r, +0x79 t, +0x7A a, +0x7B e, +0x7C d; other letters always), after which the spell is rebuilt;
EndConstruct rebuilds once more; CSpell::Clone makes the result; SetManager(null). **Build (0068BF60)** recomputes the
container's spell from scratch each time (offset mapping and modifier letters a, e, m, r, d, t, fe/ff/fg/fh/fl/fo, ic/it in
`spell_cast.cpp`); the code string is sorted (insertion sort here, introsort in the original: same bytes). CSpellEffect +0x54
is this spell's duration (named `targetCount`; for attack spells it may be a target count), +0x58 effect (`damage`), +0x5C
radius. Build's `d` / `t` truncation and the area sqrt are small naked helpers (FILD qword / FMUL / `_ftol`; FLD / FDIV
[00743A7C] / FSQRT); its double verify run is safe because it empties the code string first.

Quirks: LoadSpell reads flags_ through the stream argument's stack slot. Build leaves the target flags alone when the
prototype kind is neither 'u' nor 't', so they accumulate across rebuilds until Reset; a modifier whose code is not found
becomes code 0, which sorts first and empties the modifier list; BeginConstruct writes the prototype into the new container
without checking the manager returned one; the container's AddModifier reports a null record as "Invalid rune" (007959F0).

Not rewritten: 0068D310, 0068D450, 0068D5A0 (CLight colour contributions: a 4-float colour by value whose 4th component is
uninitialised stack, cannot be compared bit for bit; AddDirectional/AddPoint 0068D310 / 0068D5A0 are P with r, g, b CheckNear:
base + colour x scale x factor, clamped at 1, the alpha not written when lit); STLport helpers 0068C500..0068CD50, the
Rebirth-patched 00684550 (spell database load), 0068E640, 00691500. No callers: 00684180 / 00684280 / 006842E0 / 00684310 /
006838E0.

**Spell manager, containers, lights (A-H23; spell_manager.cpp, light.cpp; wrappers verify_spell_manager / verify_light).**
CSpellManager: 00688EC0 CreateSpellObject, 00688CD0 NewObject (slot 14), 00688B30 / 00688B40 Create / Destroy,
`SpellSequence::Destruct` 006886F0. The +0xBC map holds `SpellSequence`s (containers by the low half of a packed id);
nothing fills it (CreateSpellObject's only caller, Clone, passes -1), so the id path is kept as written: low half 0 inits
from the prototype, else FatalError without a sequence, and a new entry's null container faults in CopyTo. Destroy deletes
sequences without freeing their bucket arrays (leak kept); every walk stops at the first null value. CSpellContainer: +0x8C
is an (always empty) CString. CLight (0x134; the first 0x54 bytes are a CTransformObject): 0068CED0 constructor (range
sqrt(FLT_MAX): at 24-bit precision the square is 2^128 - 2^105; CheckNear), 0068D010 SetPoint, 0068D0A0 SetDirectional (also
TiltTowards). CLightFactory ("CLightManager", 0xD8): 0068DA40 NewObject (0x2E / 0x31, else DebugLog), 0068DAD0 CreateObject
(clears flags bit 7, no null check). Allocating and releasing functions are O; the light's constructor and setters S on
scratch copies; names P.

## Spell effects (0067E6D0-00683950; scenarios spells, spells_cast)

`spells.txt`: healing, six buffs, lightning, arrow, acid ray, fireball, fire wall, acid fog. `spells_cast.txt`: inv_lit (light
strike), acid_column, litnwall (light wall), clairvoyence, vision_fog (names from the maps' .mob).

`CSpellEffect` (vtable 0074399C, 0xA8 bytes; world.h calls it `Projectile`) is one cast spell and embeds its CSpell at +0x34.
+0x08 world (sector map at +0x170 + 0x124), +0x0C bit 0 "caster not credited", +0x10 position, +0x1C target, +0x20 caster.
The world server ticks `Update` (0067E6D0); +0x30 is the state: 0 casting (`Start` 0067F870, 9 KB, a switch on the prototype
index +0x4C >> 16), 1 missile in flight (`UpdateMissile` 00681BB0), 2 lasting (`UpdateLasting` 00682650), else it ends
(footprint cleared, the three effect objects dropped, Update returns 0). The three counted effect objects, also in the world's
timed-object list: +0x90 visual (missile, particle source, wall, lightning), +0x94 sound, +0x98 light; replacing one removes
the old from the timed objects and releases it (`Assign`; the original repeats it inline). +0xA0 is the state's timer.

Spell types: 0 / 0x28 fire arrow, 1 / 0x29 lightning, 2 acid ray, 3 fireball, 4 light strike, 5 acid column, 6 fire wall, 7
light wall, 8 poison cloud (acid_fog), 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26 buffs (the target's slot 45 gets a
0x14-byte record: the spell's +0x1C..+0x2F with byte 1 = trunc(power x 16) and byte 0x10 = +0xA4), 0x12 fireworks, 0x14
clairvoyance, 0x15 possession, 0x17 vision fog, 0x18 healing, 0x1B link, 0x1C teleport, 0x24 charm, 0x27 a counting area
cloud.

"Power" factor (inlined about twenty times): `pow(clamp(damage / prototype effect, 1, 8), 0.33) - 0.2`, or 1 when the
prototype's effect (+0x2C) compares equal to 0 (NaN included); clamps select float bits on FCOMP's C0 (NaN passes), pow is
FYL2X / F2XM1 / FSCALE with 0.33 from the double at 007439A8 (naked `ClampPowCore`, `PowerCore`). The fireball blast uses
the same curve on `(r*r*pi) / prototype area`.

Hits: HitUnit (006835B0) skips units the spell may not affect (only checked with a caster) and calls CUnit slot 31 (the
controller's TakeHit); HitUnitsInRadius (00683600, fireball) and HitUnitsAlong (00683730, fire wall, every 4th tick) collect
units (FindUnitsInRadius 005C0BD0 into a stack CPtrList, vtable 0073D454) and hit those IsSpellTarget (00681B80, P) accepts
(size class set, or unit prototype +0x24 below 0.5); the wall only those within 0.5 of the vertical plane through the
position along the direction (normal built with length 2, distance compared with 1). Start/Update/UpdateMissile (2.6 KB)/
UpdateLasting (3.8 KB) are O (states, parameters +0x4C..+0x60 unchanged); the visual/sound/light creators are O:
`SetVisual`, `UpdateFootprint` (stamped flag follows `stamp` for 6, 7, 8, 0x27), `CreateArrow` (class 0x4E), `CreateParticle`
(0x4A / 0x5A), `CreateLightning` (0x4D, with a target), `CreateSound`, `CreateSoundSequence` (class 99, max(count, 1)
names), `CreatePointLight` (0x4B, flags 0x840), ReleaseVisual / Sound / Light.

`CSpellEffect::Load` / `Save` 0067E7D0 / 0067E960 (spell_effect_save; Save S into capture streams, Load O: the original Save
writes back what ours read; no scenario saves during a cast). Load takes the world from 007C84EC, the three objects from its
timed object manager, and stamps the footprint; the packed id and object ids are read through stack slots holding the stream
and `this`. Every sequence with more than one FP operation before a store is a naked helper in `spell_effect_internal.h`.
Rebirth-patched callee: 005C11D0 (CSectorMap::Move, the teleport).

Quirks: lightning (1) builds its light at the target's slot-48 position without checking the target; healing (0x18) and charm
(0x24) call the target's class id without a null check; arrow and acid ray impacts read the target after the hit unchecked;
the light's SetVelocity in state 2 does not check the light. Charm with a target or caster that is not a unit returns without
changing state, so the effect restarts every tick. The light strike (4) uses -7 as bolt width factor; the walls compute the
normal with a z of 0 * k. Fireworks, clairvoyance and possession keep a unit list on the stack for the whole function, emptied
at the end. Risk: scenario wolves are placed 5 to 13 units from the hero without checking terrain (blocked ground fails the
`IsAlive` waitfor). Not rewritten: 0067AC70 (missile target), 0067C640 (CEffectShowVisibility ids), 005B2A00 / 005B2760
(footprint builders), 005C98C0 (AI noise), 005CBBE0, 00554450; 0067F360 (no callers).

## Quest database (006AE8E0, quest_database.cpp, quest_records.cpp)

`QuestDatabaseLoad` (thiscall on the database 007C87A8, RET 4, bool in AL) is called by the quest registration 005810A0, so
by every scenario that registers quests. Verified by running the original into a fresh database (constructor 006AE0F0,
destructor 006ADF40; both in library.csv) and ours into the real one: the records ours appended must equal the original's
whose names the real database did not have yet. A record is added unless the name index already holds it with a record; after
push_back the index is cleared and rebuilt over the whole vector (0064EE90). One briefing record is filled three times
("briefing receive" reads its six lists inline, "reject" two inline and four by 006AE740, "complete" all six by 006AE740).
Records are raw blocks with the original layouts, containers go through the original members. Risk: no unwinding for the raw
records if a member throws. ReadStringList 006AE740 (verify_quest_lists.cpp, S into a scratch vector; the loader calls it by
address). 0064F550 is a second copy of QuestTable::Find (004C9C90), registered on the same method (S; no scenario: only the
sort's median calls it, with more than 16 quests, all with records: the sort puts a quest without a record before every quest,
so with unknown names the unguarded partition 0064F650 runs past the array in the original too; X26 dropped the
`ui_objectives_sort` scenario that crashed this way).

In archives: `QuestRecord::Serialize` 006AD7D0 (0x38 bytes; places through vector<float> fill insert 006ADC10),
`QuestBriefingRecord::Serialize` 006AD900 (0x6C; list 0 through 0052DF40, lists 1-5 inline), the vectors
`ChunkArchive::SerializeQuests` / `SerializeBriefings` 006B1F20 / 006B2010 (they call the record serialisers by address; the
database's archive 006AE3A0 is Rebirth-patched). Records read twice (ours, then the original) and compared; saving and the
vectors O. Reached by saving and loading a game (ui_saveload, world_load).

## Textures: mmp and composed (texture_mmp, texture_compose)

Verification: `CreateFromMmp` 00690430, `LoadMmpExtra` O; `MmpHeader::MipOffset` P; `ComposeTexture` 00693180 (RET 0x1C) O;
`FillSurface` 0068FA10 (O: result = FilledBytes over the chain), `CopyToSurface`, `ConvertToSurface` (8888 to 16 bits),
`UnpackToSurface` (DXT through the converter's slot 10): S (the original fills, levels read back and overwritten, ours fills,
compared; texture_fill); `CTextureManager::NewObject` 00691810 (manager slot 14; constructor and reset inlined) O, the extra
block's `AlignedArray<uint8_t>::SetSize` 00694300 S on a copy; `SetDevice` 006913A0 (texture.cpp; quality 0079BCDC to the mip
level, the 512 limit from GetCaps, a tenth of the local video memory at +0xDC; world_client_setup calls it by address; S with
references undone between the runs); `DeleteComposedTextures` 006916F0 (texture_cache; cdecl: every <data path>textures\*.mmp
through the game's DeleteFileA, failures to the debug log; O counted only: the harness overlay decides what a delete does);
`LoadBmp` 00690260 (alpha "a_<file>" beside it; no scenario: no loose .bmp).

ComposeTexture: the cache file `<game folder>textures\<name>.mmp` keeps the request's layer bytes in its extra block (header
+0x48), so an unchanged request is reused (1). When `name` has no file and differs from the fallback name, the fallback's
file is tried. No layer textures deletes the file (0); else the image is drawn by 00693010 (`CTextureManager::DrawLayers`, S
on an image copy; returns the layers drawn, converter slot 5 per layer) and written, each level converted to 16 bits by the
pixel converter 007AF06C slot 2 [out, in, kind, count] (kind 3 for 4444, 2 for 5551, 0 for 5650, else -1) and halved by slot 7.
A file that cannot be created goes to FatalError (the image block leaks, as in the original). 00693970 and 0068F860 (a texture
from a surface description; its one caller 00691D50 is dead) are dead. `FindTypedTexture` 00692D30 and `FreeCache` 00692F00
(texture_cache.cpp, O). Not rewritten: Rebirth-patched slot 2 00691500. The fills keep a reference on every attached level
they move to (never released; the wrappers drop their second run's); a DXT file / surface pair without a decode mode passes the
mip level as the mode.

**Texture manager lifetime (A-H23; texture_manager_lifetime.cpp, verify_texture_manager_lifetime: O, names P).** 00691140
constructor, 00691250 / 00691230 destructors, 00691630 Destroy (slot 3), 00691AA0 CreateObject (slot 23: flags_ |= 0x80 /
0x100 / 0x200 for types 0x3E / 0x3F / 0x40, else 0x40; no null check), 00691200 slot 25 ("textures"). Destroy and the
destructor release the shadow surface, destroy and delete every cached texture, delete the composed textures and drop the
device; the destructor adds SetDevice(null).

## Registry and ini (registry, ini_file, win_registry; every scenario)

`CFileRegistry` (00694520-00694DC0, 00695410, 00695600, 006958E0: construct / destroy / open / attach / IsValid / close, O;
getters S into scratch objects; `FindData` 006946D0 P). `CIniFile` (00695B80-00697100: construct, readers, integer lists into
aligned / MFC arrays; readers S (ini reads repeat); writers 00696E60..00697190 not wrapped). `CWinRegistry` (006972A0-006988A0;
one HKEY at +0): construct, Destroy / Close, Open (RET 8), SetDWord / SetInt, SetFloat (4-byte REG_BINARY), SetString(Int),
GetDWord / GetInt, GetFloat, GetString(Int) (256-byte stack buffer, malloc on ERROR_MORE_DATA): S on scratch objects (a value
written twice).

CWinRegistry names come from its debug messages; each getter / setter opens `subkey` below the key (KEY_ALL_ACCESS; setters
RegCreateKeyExA with class ""), reads or writes one value, closes it. Reached at startup by the options loader (0045B210) and
on exit by the writer (0045C560), whose wrapper records the RegCreateKeyExA / RegSetValueExA import slots for both runs.
SetString / GetString call their workers through the workers' addresses. GetFloat reads into the stack slot of `subkey` (a
binary value shorter than 4 bytes keeps the pointer's remaining bytes); GetDWord's value local is uninitialised. Not
rewritten: 00698E70..00698FB0 (static initialisers zeroing five 3-dword globals before WinMain).

## Trader stock (item_stock; scenario trader_stock)

`ItemStock` lists (0xB8 bytes at ClassTemplate +0x11C "allowed" and +0x1D4 "magic"; `baseLevels` +0x78, `scrollLevels` +0x98,
copied from 004A6DC0's arguments by SetLevels).

**Reaching it.** The camp's Create (0060C870; the tail Ghidra splits as 0060DD2E) calls 004A6DF0 only when the camp has a
constructor record (+0x198 "constr.current" > 0, found with ClassTemplatesFind 004A74F0) whose +0x38 flag is set (by the
template's constructor and again by quest completion 004A88E0); the flag is cleared after generating. In the game a base's
dialogue answer "constrN" (006064E0) sets "constr.current" = N (00578560) and asks for screen 5; the harness step `screen 5
<N>` does the same from zone1 (fails when no template N exists). A new game has templates 1, 3, 4, 5 ("b.smith.constr.1",
"b.Shopper.constr.3", "b.kuzn.constr.4", "b.golem.constr.5", 005800C0); the scenario opens the camp with each. `ui_camp`
opens the camp with "constr.current" 0 and never reaches the generators. 004A6DF0 (on the "allowed" stock) calls CopyMagic
with the "magic" stock, ResetMaterialUnits, the item generators, their instructions (1, 3) and the materials. 004A6F60 (on the
"magic" stock, when the template's +0xD flag is set) calls GenerateScrolls (40, 50), GenerateSpellInstructions (1, 3) and
GenerateModifierInstructions (20, 50) into a list handed to 00661DB0. 004A6DF0, 004A6F60, 004A6DC0 are rewritten in
class_template_stock.cpp (script.md).

Functions (O unless marked): `GenerateItems` 0069AE20 (RET 0x14; via 0069B570 weapons / 0069B550 armors / 0069B590 quick
items), `GenerateMaterials` 0069A110 (RET 0xC), Generate{Weapon,Armor,QuickItem,Spell,Modifier}Instructions (RET 0xC; "scroll"
quick items skipped), `FillSpellMaps`, `GenerateSpells`, `GenerateScrolls`, `LoadLists` 0069B5B0, `SaveLists` 0069BAC0 (S;
counts and names; a spell's code: the first 4 bytes through a CString, where the original adds an uninitialised stack byte
after a 4-letter code; party_manager.cpp calls it by address; ui_saveload), `ResetMaterialUnits` 006993B0 (S; units and rand
seed restored), `CopyMagic`, `SetLevels`.

Non-obvious: the count is min + (rand() * range / 0x7FFF) % range, and more prototypes than that shuffles the template's
prototype vector itself; a material candidate is drawn with one rand() against quality q (material +0x10) and price p
(+0x34): 10% when q < 2p/7 - p/5 or q > p/5, else trunc(110 - |(q - p/7) / (p/7 - p/5)| x 100)%; instruction items get +0x54
= (record - table start) / record size (signed); FillSpellMaps keeps one entry per modifier kind letter a, d, e, m, r, t
(replacing a held one unless at least as strong; FCOMP, TEST AH,1: less or unordered replaces) and f / i under both letters;
GenerateSpells keeps the level reached in a register (the stored level is not updated); LoadLists reuses its count and three
material ints for every section (a failed read keeps the previous value). The maps 007C851C / 007C853C hold pointers into the
spell (0x84) and modifier (0x24) tables (006AC970 turns them into indices). The template's prototype and material vectors are
shuffled in place; an item whose build fails (EndCreateItem null) is dereferenced; the 004ABD00 / 00684B20 lookups insert a null
entry for every unknown name; a material entry with an unknown material is loaded with a null record. **Open:** LoadLists is
reached by loading a save with class templates (004A6C40); which scenario reaches it is not confirmed (world_load /
ui_saveload expected); record slot 0 `Kind()` (item_stock.cpp) was named from an old comment and not re-checked in Ghidra.

## What is left in 0x00640000-0x006C0000

Classification: functions.csv kind game, minus library.csv, dead_code.csv and EI_REPLACE (`python tools/progress.py` for the
numbers). library.csv holds this range's STLport / MFC instantiations (quest, item and spell database containers, portrait
slots, options vectors, twins of existing helpers); 006B4000-006C0000 was reviewed in A-H13 (sound.md). 00646C25 is a tail of
006462A0 that Ghidra splits off. Most of what is left in 00640000-00660000 is Rebirth-patched (ui.md section 7 lists them).
The server world's +0x144 is a CFileRegistry (`CWorldServer::Messages()`); the login handler 0066F890 is in objects.md, its
name check 0047ADA0 `IsPrintableText` (S).

## Interface controls 0065AEA0-0065E880 (A-H19, A-H20)

Code ui_scrollbar.cpp, ui_spin.cpp, ui_edit_field.cpp, ui_scroll_text.cpp, ui_tooltip.cpp, ui_control_delete.cpp; wrappers
verify_<file>.cpp (scenarios in their headers: objectives, save / load and options windows, every log text). Verification:
CUiScrollbar Construct/SetRect/CUiSlider::Construct O, SetPosition/SetMaximum/Maximum/thumb and slider drag/wheel S (no
scenario drags or wheels a bar); CUiSpin Construct/AddText/SetValue O, GetValue S; CUiEditField Construct O and OnChar S (no
scenario types into one: message box flag 4, multiplayer screens); CUiScrollText Construct/DestroyBody/AddText/SetText/SetRect
(26)/Draw (20)/Clear O, `Layout` (the original on a byte copy with an empty line array) and `NextToken` S; CUiTooltip
Construct (the original into a scratch block) S, SetText S, DrawLines O (OnMouseMove 0065E580 stays original: Rebirth-patched);
all their destructors O.

The scroll text constructor's float is the bar's depth, not a line height. Tokenize, Layout and DrawLines are called through
their addresses (originals_ui.h). CUiTooltip is a CUiWindow (fields from +0x148: lines_, lineHeight_, keyWidth_, keyName_).
Shared: 005FCDD0 (CUiSurface over a RECT) in originals_ui.h; CString::FindOneOf (00700ECC) in mfc.h; gamecrt::isprint
(006E944C). Quirks: SetPosition divides by the maximum without a check (0 gives the integer indefinite); the edit field's
constructor leaves caret / editing / changed unset; NextToken's unknown BEL symbol takes the token's data pointer as its
length should FatalError return; Layout reads the measured heights by the token's font digit, fonts 3-9 read the original's
other stack slots (font 3: its return address), ours 0; the tooltip's constructor leaves the measurements and both arguments
unwritten for an empty text. Risks: the constructor wrappers are ours only; Layout's stand-in shares the sprites.

## Materials (A-H24; material.cpp, verify_material: S = against the original on a scratch copy)

CMaterial constructor (names "none", alpha 1, an empty deque; colours unwritten; S), destructor body and slot 0 (O), slots 2
(reset, 1), 3 (deque emptied), 11, 12 (P); CMaterialManager constructor (colour table of NextPrime(100) buckets), destructor
body, slot 0, NewObject (slot 14), NewMaterial (slot 23, flags_ 8, the table's colours for type_; O), FillMaterial (RET 8; S),
slots 25 "materials", 11, 12 (P).

CMaterial (0x9C) is also the scene loader's helper record (SceneHelper, 00454E40 constructs arrays of them). The manager's
table (+0xBC) maps a key to a D3DMATERIAL7 (MaterialColors); an absent key is entered with the key's bits in all 17 dwords
(the original's default pair), and its Create 0068E640 (Rebirth-patched) fills it. The deque (16-byte elements, no writer
found) is `stlp::deque<T>` now; its instantiations went to library.csv. Not hookable: 0068DCF0 (slot 7, 4 bytes). Differs:
NewMaterial returns null on a failed creation (the original faults).

**Small slots and copies (A-H24).** class_names_640.cpp: the name / id slots (11 / 12) of CParty, the twelve CEffect*
classes, CSpell, CTexture, the six CItem* classes and CItemConstructor (P, verify_class_names_640.cpp); 00688940 the spell
manager's folder, 0067E150 the effect manager's Create (world_managers.cpp, O). Party containers (party_lifetime.cpp, O):
`CArray<PartyUnitRecord>` destructor body / slot 0, DestructElements, the record's deleting destructor; ui_hero_screen_click.cpp:
MpHeroRecord's deleting destructor, copy() of hero records (Assign each). Byte-identical copies registered beside their
rewrites: 00664910 / 00671240 AllocateDwords, 0069FD50 RefPtrRelease, 00689810 / 00689880 CleanupClear8. Left (under 5 bytes):
CParty's slots 0065F4B0..0065F550, 00661140..00661180, the empty `ret` slots of 00666EE0..00667670, 00691900, 0068DCF0.

## A-H25

Smaller ports (names in the code; V as above), grouped by what they are:
- Lists and creation: `CItemManager::ListMaterialNames/ListWeaponNames/ListArmorNames/ListQuickItemNames` and
  `CSpellManager::ListSpellNames/ListModifierNames` (`this` unused; every new game through 004A7080; S, texts compared),
  `CItemManager::CreateSpellItem` (the scroll of a spell text, O), `CItemConstructor::MagicCapacity` (trader_stock; S on
  scratch floats), the item records' slot 0 (kinds 0x3004 / 5 / 6 / 9 / 7; P), `ItemStock::AddArmor / AddWeapon / AddQuickItem /
  AddSpell / AddModifier / AddMaterial` (`stlp::vector::push_back`; O: present once, size +1 only when new), the item
  manager's Create (slot 2) and NewObject (slot 14: kinds 0x3004..0x3009 to weapon / armour / quick / loot / scroll / quest;
  O), `CItemConstructor::DeletingDestruct`, `RecordTableAt<Stride>` (armour 0x88, quick 0x80, 0x7C, 0x78; P),
  `WeaponModelName` / `WeaponRecordModelName` (P texts; the second compared only for weapon types 0..6, beyond them the
  original reads past its code table), `CItemSpell::PictureName` ("initqi1item"), `CItemQuest::GetUse` (slot 21: kind 8, the
  value at +0x4C, 0; P).
- Effects and textures: `CSpellEffect::Init` (every cast) / `Forget` (an object leaving the world during a cast; O),
  `MapEffect::Tick` (S), `MmpHeader::SetNoFormat / SetFormat5551 / 4444 / 8888` (texture_slots.cpp; ComposeTexture keeps its
  forwarders so the wrappers run; S), `CTexture::Create` (slot 2; block_ cleared, not freed) / Destroy (slot 3) / FreeBlock /
  DestructBody / DeletingDestruct (O), `CTextureManager::GetFileData / NewTextureOfType / FindOrCreate (named, not loaded; no
  null check) / FindOrLoad` (O), `ReloadAll` (after a lost device: no scenario), `TrimCache` (cached bytes against
  videoMemoryTenth_, signed; every 32 client ticks; O), the effect base's out-of-line destructor, slot 0, constructor, missile
  Reset, default Tick, ReadSavedStrings (effects.md; O, TickDefault P, constructor and ReadSavedStrings S).
- Records: `ArmorRecord / QuickItemRecord / QuestItemRecord / LootItemRecord` assignment (item_db_records.cpp; O),
  `ItemRecordHead::ConstructBase` (vtable 00744060), `MaterialRecordHead::CopyConstruct` (S), `QuestBriefingTable::Add`
  (quest_records.h: the database's +0x20, vector + index by name; push_back 006B0780, clear 006B0590, find 006B0930 through
  their addresses; O), `PartyUnitRecord::CarriedWeight` (CItem +0x34 is the weight; weapons, quick items, spells, armour in
  one running float sum; P with CheckNear), `CParty::ArchiveParty` (a memory stream as the archive's stream field 1; loading
  takes the archive's version; O), `MpHeroRoster::Select` (ui_screens.h: the multiplayer heroes at 007C82A0; no scenario opens
  the hero screen; O), `CPlayerClient::AppendReceived` (appends a stream to the received one, read position kept; O).
- Input and UI: `CDirectInputMouse::Setup / Shutdown / SetActive / Poll` (input_mouse.cpp; the game's own GUID copies and
  c_dfDIMouse at 00745D00; a failed step returns FatalDirectX's answer as 0 / 1; O, SetActive S: acquire twice is harmless);
  `CUiScrollingSurface` (the titles' RTF text; ui_scrolling_surface.cpp): Construct ("Riched20.dll", a hidden RichEdit20A
  child), DestructBody, DeletingDestruct, LoadRtf (EM_STREAMIN from the block: our own reader replaces the CMemFile and its
  callback 0065EF10), Draw (slot 0: offset += game time passed x speed, _ftol; O within 1), Load (S), Start (speed, from
  below the surface; S); `CUiEditField::SetLook / SetText / SetFocused` (+0x16C is the caret colour; S), `CUiSpin::OnLButtonDown`
  (input slot 2: the OK sound, the next value; O), `CObjectivesWindow::Setup` (S) / `HelpTopic` (slot 41: "quest_global_map"
  with two buttons, else "quest_mission"; P), `CHudClock::OpenObjectives` (from the Rebirth-patched click 00648D70: its inlined
  window constructor is 0063B5D0; O), `CHudUnitPanel::ShowBodyPart` (tints the four part buttons 3..6: 1 the chosen one, 0.5
  the rest; S), `CHudUnitPanel::ShowEffects` 00646B90 (Ghidra splits it at 00646C25; the unit's first six effects: the "spell
  <code>" text and ticks / 15 as two lines each, the spell's picture "spell%04d" (SpellRecord +0x60, now `icon`) copied into
  one of six places of the panel's icon texture, the places kept in +0x184 `effectIcons_`; scene objects 26 + i show them, 9
  when there are more than six; S, needs a shown unit with an effect), `NextSpellContainer` (a hash_map iterator's step; P, no
  live caller left).
- Sound: `CSoundManager::OpenStream / CloseStream` 006BAAC0 / 006BAC90 (on the manager's +0x100): the ASI codec for ".mp3"
  (RIB_find_file_provider, stdcall: mss32's RET 0xC), the playing sample and the decoding one (+0x1AC `streamDecoder`;
  `streamCodec` is its +0x144), three buffers +0x110 of +0x11C bytes (twice Miles' minimum for 44100 Hz stereo 16-bit), the
  order 0 / 1 / 2 at +0x124, the fader as SetHold(0) leaves it (O; Open at every start with sound on; CloseStream only when the
  stream cannot open); `CSoundManager::StreamThread` 006BB910 (every 100 ms under the stream lock: the fader's step, the level
  as the sample's volume, each played-out half of Miles' double buffer gets the spare buffer (+0x124 `streamHalfBuffer[2]`,
  +0x12C `streamSpareBuffer`) and 006BB3A0 reads the next; AfxEndThread when threadsQuit is set; O: the state it starts from, it
  does not return); `SoundEndOfSample` 006B75E0 (Miles' end-of-sample callback, on Miles' thread: the voice in user data 1 is
  finished; O: the voice is the sample's, the mark is taken by the game's thread at once).

Library, second batch: 0064EE90, 00652E30, 00652F80, 006581C0, 00675040, 00675690 (STLport); compiler: 00652FF0, 0067C5D0 (unwind
only), the static initialisers 00698E70..00698FB0 and 0065F580 (run before the hooks), the scalar deleting destructors 006611B0,
006868D0, 006868F0, 006B2DE0, 006B2E00, 00663570; 006B6CC0 (STLport string default constructor). Also library: 006A5910, 006AF790,
006B0710, 006B0780, 006B09B0, 0069D0E0, 0069E350..0069E3D0 (STLport helpers of the record tables and the stock), the records' scalar
deleting destructors 0069E540, 006A9EF0..006A9F90, the by-name lookups 006AD580 / 0069D290 / 0069D380 (hash_map operator[](const
char*), as 00613820), the code string's sort and reserve 0068C680..0068CE00, the trader stock's vector / list / pair helpers
0069BD60..0069E4E0, unwind-only vector destructors 0064AA40 / 0065A860 / 00663410 / 00675560, 00663660, 006726E0.

**Left in 0x640000-0x6C0000 after A-H25** (apart from the Rebirth-patched ones): 0066D730 (CPlayerServer's message 0x12 sends an
uninitialised stack byte; see A-Z1), 0067E4E0 (the constructor of the global at 007C8478, called only by the static initialiser
0067E4C0, before the hooks), 006BFB00 / 006BFB20 / 006BFB80 (SectorData: A-Z1), functions under 5 bytes, and two Ghidra artefacts
(00646C25 is the body of 00646B90, 00656464 is inside an instruction). Rebirth-patched: 00641F20, 00642930, 00642B40, 00645210,
006456E0, 00645F80, 006462A0, 00647FB0, 00648520, 00648D70, 00649190, 00649910, 006567A0, 00659420, 0065C2E0, 0065E580, 0066C180,
00684550, 0068E640, 00691500, 006A67A0, 006AE3A0, 006B7110, 006BB3A0 (A-H26 ports their vanilla bodies).

## A-H26: the Rebirth-patched functions' vanilla bodies

Each one is the function as the exe on disk has it. The installer skips a function whose bytes differ in memory, so these
hook in the vanilla launch only and their wrappers run only there; our callers keep going through the EI_ORIGINAL forwarder
(rule 9), which is why the UI ones are named `...Vanilla` beside a forwarder of the plain name. Patched callees are reached
through their addresses: 00671730 / 006714A0 (resource open / destructor), 00480080 (FatalError), 005E6FA0 (sprite Place).
V: S side by side, O ours only. Behaviour is in the code (file in brackets); the notes below are what the code does not make
obvious.

- 0066C180 `CPlayerManager::CreateSessionPlayer` (player_manager.cpp; O): a player 0x300A with a connection 0x49 of the
  world's specific manager, refCount + 1, player id 0. 00691500 `CTextureManager::Create` (slot 2,
  texture_manager_lifetime.cpp; O): "textures.res", "redress.res", "menus.res" of the text folder (007AF45C) all tried, 1 only
  when all three opened. 006B7110 `CSoundManager::ListWaveFiles` (sound_manager_lifetime.cpp; S): runs on the game's main
  thread (only caller 0053A8E0, UnitDatabase::CountSoundFiles).
- Database loads (O: counts, every record's key leads to a record of that key; every start and each switch of the resource
  database (0057CB50)): 00684550 `SpellDatabase::Load` ("spells.sdb": spells, modifiers, three lists) and 006A67A0
  `ItemDatabase::Load` ("items.idb": materials, weapons, armors, then quick items, loot, quest items) in item_db_open.cpp;
  006D0630 `PrintsDatabase::Load` and 006D7620 `acks::AckDatabase::Load` (model_db_open.cpp; models.md); 006AE3A0
  `QuestDatabase::Load` (quest_db_open.cpp; the database at 007C87A8 is `QuestDatabase` in quest_records.h: quests + index,
  briefings + index). 0068E640 `CMaterialManager::Create` (slot 2, material.cpp; O): "materials.res" added, the eight kinds of
  the table at 00743BF8 (0x11 leather .. 0x17 magic, 0x19 metal again) entered with their file's 17 dwords, or diffuse 0.3 /
  alpha 1, ambient 0.7, specular 0.2, power 5 without the file; magic's emissive is 1.
- HUD ports (ui_hud*.cpp, ui_hud_slide.cpp, ui_hud_setup.cpp; the code is the description): 00648D70 `CHudClock::OnMouseUp`
  (regions 0 pause, 1 / 2 normal / fast speed, not on a network client, update period 55 / 27 ms at 0078FF34, 3 the
  objectives; O, no scenario clicks the clock); 00649910 `CHudMovePanel::ShowModeVanilla` (+0x14C `mode_`; S); 00642930 /
  00645F80 map and unit panel `LayoutVanilla` (sprites, text surface and regions after the first at the slide offset:
  `offset_`, `travel_`, `slide_`; S); 00647FB0 `CHudUnitPanel::UpdateVanilla` and 00642B40 `CHudMap::UpdateVanilla` (slot 24:
  the slide time x direction / 0.3 s, eased, _ftol of travel x eased; the map's zoom x or / 2.5^time towards zoomMax_ / 1,
  UpdateView, BuildMarkers, "camera north" time and tint; the map's is O because it rebuilds meshes, no scenario slides the
  map); 00645210 `CHudMap::OnMouseUp` (O; ui_hud reaches only the view buttons' release, no scenario clicks the map, its
  sling or the north button; a click on the map goes through 005F7E00 relative to (700, 80) by the map's scale
  `unknown_174`, region 1 also by the zoom). Setups (S: the original's regions and sprites recorded and dropped, the fields
  put back, then ours; every scenario that starts a game): 00648520 clock (rectangle (720, 520)-(800, 600); tips 0x28A0,
  0x28A2 / 0x28A3 not on a network client, 0x28A1), 00649190 move panel (slot 18; rectangle (0, 520)-(80, 600); tips
  0x2904..0x2908, keys 0x1B, 0x17..0x1A), 006456E0 unit panel (rectangle (0, 0)-(185, 225), the slide at its far end when
  `slid`, offset 180; tips 0x2775.., keys 0x2E..; "infounit_0..17" and the four "hl_" labels), 00641F20 map (+0x174
  `halfSide_`, +0x178 `viewPolygon_`; rectangle (615, 0)-(800, 165); the world's extent is the zone record's size x 32,
  picture referenced object 1, 32 x 32 and "battle00" without a record; scale 2 / size, zoom up to size x 0.05; the round
  map is sixteen points of radius 65 about (700, 80); a 5 x 5 surface with one white pixel; the picture's reference and
  place are given back without a release so the second run finds the same texture).
- Other windows: 0065E580 `CUiTooltip::OnMouseMoveVanilla` (ui_tooltip.cpp; +0x17C `timer_`; O: it makes the surface): state 1
  (hovered) gives 0 with no lines or a zero delay (setting 20, 0079BE80), else the timer runs up to the delay, then the
  command's key (FindCommandByKey of `extra_`) is measured with the first line, state 2, a surface (format 5) at the mouse +
  the cursor offset ((24, 24) screen pixels at 007C8258, set by the static initialiser 0065E310), pushed left off the right
  edge and above the mouse at the bottom; state 2 draws and becomes 4 (the appearing time 00742F2C is 0); state 3 never
  runs. 0065C2E0 `CUiEditField::DrawContentsVanilla` (ui_edit_field.cpp; +0x15C `centred_`; S on the clamped caret, pixels
  not compared). 006567A0 `CSaveLoadWindow::RefreshListVanilla` (ui_saveload_list.cpp; S; ui_saveload, ui_saveload_delete,
  ui_hud): up to sixteen rows from the scroll position (title, or the typed name for the selected row of a save window; the
  date "%d/%m, %H:%M" through CTime::Format 006FE795 but for a save window's new save), the caret (not for "saveq" / "savea" /
  "saveb"), the marker (scene object 4); then the selected save's allod, zone, day and time (_ftol(gameTime x 60) minutes: day
  + 1, h:mm) and folder. 00659420 `CHelpWindow::ShowPageVanilla` (ui_help_page.cpp; +0x18C `pictures_`; S, the function
  empties the window itself; ui_help): "tutorial <text_>" opened, the page's "#screen" found; rectangle (200, 50)-(600, 560)
  or "#position left top right [rows]" (searched from the page on; at least 300 wide, rows x 16 + 110 high, at most 600);
  surface 0 title, surface 1 the entries' "tutor <key>" texts (laid out twice unless rows are given: the first pass copies
  "tutorial_<picture>" into 64-pixel cells of referenced object 1, at most 16), surface 2 neighbours' titles and
  "show_tutorial_key"; regions 0-2 the buttons (tips 0x7595, 0x7594, 0x7596). With a row count in "#position" the measuring
  pass is skipped and with it the pictures; with sixteen pictures every text is laid out unindented in the second pass.
- 006BB3A0 `CSoundManager::ReadStreamVanilla` (sound_manager_lifetime.cpp; +0x120 `streamBufferFrames`, +0x138 `streamLoopAt`,
  +0x13C `streamFrameBytes`, +0x140 `streamSilence`; O: it decodes as it goes, so it cannot run twice): fills the spare
  buffer through the ASI codec (slots 1 read, 2 seek, 3 close, 0 open, 4 attribute; names at 11 / 12 / 13: output rate, bits,
  channels) up to the loop point (the first "Markers2" value; then seek 0) or the switch point (mode 2: the whole buffer once
  the fade out has finished, the fader held for 4 s; mode 1: where the file ends); silence when idle or a file ends early; the
  switch opens soundPath + nextName through the stream reader ("Markers1" / "Markers2" from the sound registry, decoder
  closed and opened with the read callback 006BA950). It is called only by 006BB910 `StreamThread`, the game's own thread
  (AfxBeginThread), under the stream lock every 100 ms; not Miles' thread, so it has a wrapper (the fader's step 006B5EE0 is
  already checked on that thread). The original leaves its "from the beginning again" local unwritten when neither a loop
  point nor a switch point lies in the buffer and reads it in one such case (a mode 1 switch whose file ends in this buffer:
  stack garbage decides between the switch and a restart); ours switches, which is what the mode asks for.
- 006462A0 (unit panel draw) was ported by A-E2 (`ui_hud_unit_draw.cpp`, `CHudUnitPanel::DrawVanilla`: Rebirth replaces the
  whole function, so vanilla launch only; ours only, every map frame): it drives the Direct3D device directly, the calls made
  as they are and mirrored to the new renderer. Between the panel's background and the unit's figure, and again after the
  figure, it clears three screen rectangles' Z-buffer (and stencil when the device has one) with IDirect3DDevice7::Clear (the
  device at the renderer's +0x54, vtable +0x28) under the render lock 007AEE78, after flushing the renderer's two batches
  (004E7840, 004E7A40); in between it draws the unit's model (the unit's slot +0x58) with texture stage 0's mip filter (state
  0x12) set to 1 and put back (vtable +0x90 / +0x94). 006C68B0 `Terrain::LoadMap` (the map loader) is ported too: models.md.

Shared: database_file.h (`dbfile::LocalResource`, `LocalStream`, `FillStream`, `LoadTable`, `OpenDatabase`, `ReportNoDatabase`;
unit_db_load.cpp uses it too); `MappedResourceOpenOriginal`, `ChunkArchiveDestroyOriginal` (originals_engine.h);
`GameClientPaused` / `GameSendFlag10` in world_glue.h. The tables' vector serialisers are called through their
addresses so that their wrappers run. The folder-only loads (quests, prints, acks) open "database.res" but report
"databases.res" when that fails; the serialisers' unused third argument is whatever the original's stack held (null in ours);
ItemDatabase / SpellDatabase / AckDatabase file a repeated key over the earlier record (operator[]);
CMaterialManager::Create ignores AddArchive's result and calls CloseFile with null for a missing file. Float notes (rule 7):
the slides compare on the FPU stack in the original; an offset on a rounding edge may differ by one for a frame (the wrappers
allow it and then skip the layout comparison).

**Arc points (00648520, 00649190, 00641F20).** The regions' vertices are `_ftol(cos(k x pi x step) x radius)` with every
product rounded to float, as the game's FPU does at its 24-bit precision, but not the cosine or sine themselves: the
precision control reaches FADD / FSUB / FMUL / FDIV / FSQRT only, FCOS and FSIN leave the full result on the stack. So
sin(pi / 2) x 34 = 33.99999.. rounds to 34 and the vertex is the whole radius (35, 80, 34; in doubles one less), and
cos(pi / 3) x 34 = 16.9999991 rounds to 17. X33: the port rounded the cosine to float first, which gives 0.49999997 x 34 =
16.999998 and 16, so the clock's middle region had its third arc point at (784, 571) where the original has (783, 571).
`OnArc` in ui_hud_setup.cpp rounds each product with RoundToFloat, so it does not depend on the precision control. The map's
outline is `(_ftol(700 - sin x 65), _ftol(cos x 65 + 80))` the same way: its point under the centre is (635, 80), where
doubles give (635, 79). The setups' wrappers name the region or sprite field that differs and both values
(`DescribeRegionDifference` / `DescribeSpriteDifference`, `tests/verify/ui_panel_shot.h`, which also holds the setups' record
of regions and sprites). New forwarders: `CUiRegion::CallAddPoint` (005FCA70) and `CUiMesh::SetTriangle` (005E99D0), both in
ui_layouts.cpp.

## A-Z1: the last eight hookable functions without a replacement

- 006BFB00 / 006BFB20 / 006BFB80 `SectorData::IsDrawn` (+0x60 is 0 or 1), `ClipTest` (the box's verdict at +0x60, -1 stored as
  2; unless culled the four quarters' in `quarterMode`), `FindSeams` (the two seam flags from the lit neighbours' level of
  detail; lookups without create) in `terrain_passes.cpp`: S, every map frame (DrawPages calls the three through their
  addresses; RunTick calls IsDrawn directly).
- 0066D730 `CPlayerServer::SendValue12` (`player_messages.cpp`): message 0x12, a byte and the value; S with the stream restored,
  the unwritten byte left out (the script command SendEvent through CPlayerManager::SendValueToAll calls it by address). The
  original never writes the first payload byte: what goes out is the stack byte 8 below its entry ESP, which cannot be
  reproduced and nothing reads (the client's handler 00667E20 skips the byte); ours sends 0.
- 0067E4E0 `FootprintCellSet::Construct` (`ai_footprint.cpp`): the constructor of the spells' cell set 007C8478 (empty array,
  `hashed` 1; the buckets not written); start-up check (it runs from the static initialiser 0067E4C0, before the hooks): the
  five written fields against the original's global.
- 00695BE0: not game code (CIniFile's compiler-generated destructor, a JMP to CString::~CString; config/library.csv).
- 00646C25, 00656464: not functions. The strings "%ld" (0074676C) and "dde" (0074718C) have the bytes of these two code
  addresses, so Ghidra made a function at each (both inside an instruction) and ended the real functions 00646B90 / 00655D40
  there. Their rows in config/vtables.csv were dropped, and `src/hook/function_sizes.inc` / `config/extra_function_sizes.csv`
  now give 00646B90 1167 bytes and 00655D40 2301 (tools/gen_function_sizes.py lets a row there replace Ghidra's size), so the
  installer's comparison with the exe on disk and check_signatures see the whole bodies. No Rebirth site lies in either tail.
  (Running the generator today changes many other sizes of the committed list: Ghidra's first body range is shorter than what
  the list holds. Not regenerated.)

Left in the game range: functions under five bytes only.
