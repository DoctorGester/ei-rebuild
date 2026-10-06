# Items, spells and related data (rules, texts, textures, registry, trader stock)

Layouts, record sizes, field offsets and vtable addresses are in the headers (`item_db.h`, `item.h`,
`spell.h`, `item_stock.h`, `party.h`, `texture.h`) and the code headers summarise each file; this file
holds what they do not say. Code is in `src/game/`, wrappers in `tests/verify/verify_<name>.cpp`.

| Subsystem | Code | Verify | Scenario |
|---|---|---|---|
| Item rules | item | item | items |
| Item parts, constructor, names | item_parts, item_constructor, item_names, item_model_names | item_parts, item_constructor, item_names | items, ui_camp |
| Item texts | item_db_text | item_db | items |
| Spell rules, construction | spell, spell_cast, spell_text | spell, spell_cast | spells |
| Spell effects | spell_effect, spell_effect_start, spell_effect_update | spell_effect | spells, spells_cast |
| Quest database | quest_database | quest_database | any new game in zone1 |
| Textures (mmp, composed) | texture_mmp, texture_compose | texture_mmp, texture_compose | all (skins at zone load) |
| Registry, ini | registry, ini_file, win_registry | registry, ini_file, win_registry | all |
| Trader stock | item_stock | item_stock | trader_stock |
| Party item lists | party_items | party_items | ui_camp, trader_stock, party_records, items |
| HUD panels | ui_hud_panels | ui_hud_panels | ui_hud |

Stateful functions (create/release counted objects, deal damage, draw from ISAAC, write files) are
verified "ours only" with invariants on the result; pure ones side by side. Jump tables (0068B700,
0068B770, 00683950, 006A0110, 006A4230, 006A5470, 0068BEC0) were read from the exe, not the decompiler.
Callers' forwarders stay and reach the rewrites through their addresses, so wrappers run.

## Databases and naming

Eight STLport vectors of fixed-size records (007C86D8 materials, 007C86F8 weapons, 007C8718 armors, 007C8738
quick items and spell containers, 007C8758 / 007C8778 quest and loot items, 007C8510 spells, 007C8530 spell
modifiers). A lookup checks `index < (end - begin) / stride` (signed division, unsigned compare), else null;
callers then read fields of the null record, so an index out of range faults (kept: `Field<T>()`).

**Known naming mismatch:** the game treats 0x3007 as the loot item ("litem", item_loot, CItemLoot kind at
+0x4C, table 007C8778 with 0x78-byte records) and 0x3009 as the quest item ("questitem", item_quest, table
007C8758, 0x7C). `item.h` / `item_db.h` name them the other way (`kItemQuest` = 0x3007, `QuestItems()` =
007C8778, `kItemLoot` = 0x3009, `Loot()` = 007C8758). The swap is consistent, so the code is right.

Weapon stats as the unit stats code (00538960) combines them: min damage = weapon damage x material damage,
max = (spread + damage) x material damage, attack = weapon attack x material attack, damage type = first
non-zero weight (6 if none).

Record values (item_db_records.cpp, A-H6; ours only, every run: the vectors grow while the databases load): 006A8F20
ItemRecordHead::Assign (three strings, +0x0C, +0x14..+0x1C, +0x34..+0x4C, byte +0x68, CStringArray::Copy of +0x20; the
+0x50 array, +0x64 and +0x6C are not copied) and the five kinds' uninitialized_copy / uninitialized_fill_n (__fastcall, RET 8,
the second stack argument an unused tag): weapons 006AB9C0 / 006ABB50 (vtable 00744068; own bytes +0x6C..+0x83, +0x8C..+0xB4),
armour 006ABCD0 / 006ABE20 (00744064; +0x6C..+0xB4), quick 006ABF60 / 006AC0D0 (0074405C; +0x70 and +0x80..+0x8A, the string
+0x7C constructed empty, the category not copied), the 0x7C kind 006AC240 / 006AC3B0 (00744058; +0x6C, its zone vector +0x70
left empty), the 0x78 kind 006AC520 / 006AC640 (00744054; +0x6C..+0x77). Kept: the copies drop the fields listed as not
copied. Not ported (not reached while loading: the vectors start empty): the kinds' operator= (006A8FB0 ...) and destructors
(006A88F0, 006A8950, 006A89D0, 006A8A50, 006A8AE0, 006A8BD0).

## Item rules (scenario items)

| Address | Name | Verify |
|---|---|---|
| 006A0110, 006A4230 | CItem::GetPrototype, GetCategory | pure |
| 006A07F0, 006A06C0 | CItemWeapon::GetType, IsSameAs (vslot 14; calls base 0069FE90) | pure |
| 006A39E0, 006A31A0, 006A5470 | CItemArmor::GetType, GetTypeGroup, CItemLoot::GetPrice (vslot 16) | pure |
| 006A2B80, 006A2A50, 006A2AE0, 006A2BD0, 006A2C60, 006A2D40, 006A2D90, 006A3200 | WeaponType, MinDamage, MaxDamage, Attack, DamageType, Stat84, Stat88, ArmorFieldB8 (fastcall on a packed id / index) | pure, float bits |
| 006A0770 | CItemWeapon::GetDamageTypes | original into a scratch float[7] |
| 006A0070 | CItem::Recharge (vslot 6) | ours, charge restored, original; result and charge bits |
| 006A2DE0 | CItemWeapon::RollDamage (declared `double`, unrounded ST0) | ISAAC snapshot, ours, restore, original; roll bits and generator state |
| 0069FE90, 006A30D0, 006A3C20, 006A4510, 006A4B00 | IsSameAs (slot 14) of CItem (base body), armour, quick (also scrolls), quest, loot (`item_slots.cpp`) | pure |
| 006A0660, 006A3070, 006A3BE0, 006A44D0, 006A4AB0 | InitFromRecord (slot 13) of weapon, armour, quick (also scrolls), quest, loot | original on a scratch copy of the item, whole object compared |

| 0069FCA0, 006A2F20 | CItem::Construct, CItemArmor::Construct (`item_lifetime.cpp`) | original on scratch memory seeded with the real bytes, whole object compared |
| 006A0540, 006A3AD0, 006A43A0, 006A49A0, 006A56E0 | Construct of weapon, quick, quest, loot, scroll (a CALL within 5 bytes: no trampoline) | ours only: CItem's fields, vtable, the weapon's -1.0 floats |
| 0069FE20, 0069FE50 | CItem::CreateItem / DestroyItem (slots 2 / 3, every class) | ours only: count 1, spell and link (+0x1C) gone |
| 0069FD70, 006A05B0, 006A2FC0, 006A3B30, 006A4420, 006A4A00, 006A5740 / 0069FD30, 006A0590, 006A2FA0, 006A3B10, 006A4400, 006A49E0, 006A5720 | CItem::DestructBody / DeletingDestruct (slot 0), one copy per class | ours only (slot 0 without the free, the wrapper frees): CEngineObject vtable, no owner, spell or link |
| 006A0270 / 006A0360; 006A2E60 / 006A2EB0, 006A3A70 / 006A3AA0, 006A3C80 / 006A3CA0 (quick and scroll), 006A4570 / 006A4590, 006A5630 / 006A5680 | Load / Save (slots 9 / 10) of CItem, weapon, armour, quick, quest, loot (`item_save.cpp`) | Save side by side into capture streams; Load ours through a recording stream, then the class's original Save must write back exactly the bytes read |

The seven destructor bodies are byte-identical (each stores CItem's vtable; the subclasses add nothing), as are the
seven slot 0s. Load / Save: the base writes +0x18, +0x20..+0x3C, the spell's id (-1 none; Load looks it up in the
context's spell manager), +0x48, +0x40; armour adds its two absorption rows (not layerMask_). The loot's Load reads
kind_ / recordType_ through the stack slot of its stream argument, CItem's the spell id likewise (kept). Scenarios:
Save every new game's autosave, Load world_load and ui_saveload.
Constructors leave +0x40 (CItem) and the quick, quest, loot and scroll fields (+0x4C..) as they are; +0x20..+0x3C
start at -1.0. The link at +0x1C is a CRefCounted (16-bit count) that Destroy deletes at 0.
IsSameAs compares the base floats (+0x20..+0x2C, +0x34..+0x3C) as "equal or unordered" (FCOMP, TEST AH,40h) and
the armour's rows and masks as dwords; the subclasses' slot 13 inline CItem::InitFromRecord and add their record
fields (`WeaponRecord`, `ArmorRecord`, `QuickItemRecord`, `LootItemRecord` in item_db.h; the quest item takes the
head's +0x6C). Scenarios: items (weapons, armour, potion, rune, quest items; merges in the party list), trader_stock.

## Item parts, constructor and names

| Address | Name | Verify |
|---|---|---|
| 006A0FF0, 006A1780 | WeaponModelParts, AppendWeaponModelParts | original into a scratch array / vector |
| 006A0490 | CItem::InitFromRecord (slot 13) | item bytes +0x20..+0x47 |
| 006A04D0, 006A0520 | CItem::AttachSpell, SetSpell | ours only: spell and price |
| 006A5B20-006A5D30, 006A5CC0-006A5CE0, 006A5D60 | CItemConstructor: destructor body, Reset, Detach, SetManager, SetItem, Begin/EndCreateItem, Cancel, SetMaterial / SetPrototype / SetSpellObject, Build | ours only: state, packed id, count; object bytes for the setters |
| 006A0840, 006A0F70, 006A2750, 006A27E0 (weapon); 006A3240, 006A36A0, 006A3720, 006A37B0 (armor); 006A3CD0; 006A4040, 006A45C0; 006A47B0, 006A5820; 006A4B90, 006A5030 (loot) | ModelName / FirstListName / FigureName / PictureName (slots 17-20) per item class | text compared |
| 006A0CC0, 006A3480 | WeaponBaseFigure, ArmorModelName | against the original: result and text |

- Build's temporary record is built like the game's record classes (vtables 00744054..00744068); the extra
  members of the quick/quest/loot records leak as in the original. Build reads operands into locals before
  multiplying; `InitFromRecord` copies +0x40/+0x48 through FLD/FSTP so a signalling NaN is quieted the same way.
- Name builders with an unknown weapon/armor type: the original formats an uninitialised 4-byte stack
  buffer, ours "" (no such types in the databases).
- `ArmorModelName` formats "%s_%.2d.%s.%d" (type code "hl","pl","lg","sh","pt","bt","gl"; type compared
  unsigned with 6). The original copies the code into the stack slot of its material-index argument and
  formats that slot, so a type without a code yields the index's own bytes (reproduced). Callers: unit skins
  00536670 / 00568800 / 005685E0. `WeaponBaseFigure` is reached through the weapon's 006A0F50 from the unit
  effect point 00532860 (effects, spells_cast); its 4-byte number buffer would overrun above 999 (not in the
  database, not reproduced).
- `CSpellManager::CreateSpellFromText` (00689030, spell_text.cpp): ours only, a spell with an index in range.

## Party item lists and CopyTo (area ticket A-H)

All ours only, with checks of the copies' values and of the lists' counts and order.

| Address | Name |
|---|---|
| 0069FF70, 0069FFC0 | CItem::Clone (RET 4, spell manager), CopyTo (slot 15's base body, RET 8) |
| 006A0730 / 006A3140 / 006A3C50 / 006A4540 / 006A4B50 / 006A57F0 | CItemWeapon / Armor / Quick / Quest / Loot / Spell::CopyTo (slot 15): 0 for another kind, else 1 with base values and the class's own dwords equal |
| 00661DB0, 00661F30 | CParty::AddItem (RET 0xC; flags read as bytes), RemoveItem |
| 006608D0 / 00660AB0 / 00660C90 / 00660E70 and 00660A50 / 00660C30 / 00660E10 / 00660FF0 | PartyUnitRecord::AddWeapon / AddQuickItem / AddArmor / AddSpell and the four Removes (lists +0x730 / +0x740 / +0x750 / +0x760) |

Clone = the owning manager's CreateObject(kind, -1) (slot 23), then the source's slot 15; on failure the new
item gets a reference and is released. CopyTo destroys the target (slot 3), gives it +0x18 and dwords
+0x20..+0x40, a CSpell::Clone (0068B410) of the attached spell or none, count 1; the overrides call the base
through its address (`CItem::CallCopyTo`). The record adds clone with the world's spell manager (CWorld slot
22 `SpellManager`, 0055EBA0: +0x4C). The party's adds merge with a held item that is the same (slot 14), else
the party's slot 24 (0055E940: the factory makes the item, the source copies itself into it) gives the copy;
array growth is AlignedPtrArray::SetSize (00519D70). Reached by ui_camp (0060FCA0 / 0060FE40 / 0060E900),
trader_stock (004A6F60 into 00661DB0), party_records (0058FD90 / 00590C70).

## Party records and the party object (party.h, party_lifetime.cpp)

| Address | Name | Verify |
|---|---|---|
| 0065F5F0, 00664880 | PartyUnitRecord::Construct, ConstructPartyUnitRecords (stdcall: zero, then construct each) | original on a scratch copy seeded with the real bytes, all bytes compared |
| 0065F740, 0065F860 | PartyUnitRecord::Destruct, ClearItems | ours only: lists empty, defaults set |
| 00663FA0 | PartyMemberArray::SetSize (CParty +0x2C, MFC CArray<PartyUnitRecord>, vtable 00742FF4) | ours only: size, capacity, new records constructed |
| 00663960, 00663AD0, 00663C00, 00663D30 | byte-identical copies of AlignedPtrArray::SetSize (00519D70) | ours only: size, alignment |
| 00661050, 006A5AB0, 00683FF0 | CParty, CItemConstructor, CSpellConstructor::Construct | original on a scratch copy seeded with the real bytes; all bytes (the party's variable map: bucket count and emptiness) |
| 00688840 | CSpellManager::Construct (CResourceManager's, an empty map of 193 buckets at +0xBC) | ours only |
| 006611D0, 00661190, 006614F0, 00661640, 00661700 | CParty::DestructBody, DeletingDestruct (slot 0), CreateParty (slot 2), DestroyParty (slot 3), Clear (RET 8) | ours only: managers, arrays, zone name, variables as each leaves them |

A constructed record has seven empty strings, empty lists, +0x724 0.8, +0x728 0.2, height 1.0, +0x770 2, +0x774 0,
+0x778 0, +0x77C -1, +0x780 1 (the destructor sets the same). Every new game reaches them: the party record reset
adds the hero (006628D0 builds a record on the stack, grows the array, destroys the stack record).

CParty (0x70): +0x1C item manager (0xBC, CResourceManager with vtable 0073F25C), +0x20 item constructor, +0x24 spell
manager (0xD4), +0x28 spell constructor (getters: slots 18, 16, 22, 17), +0x2C records, +0x40 items, +0x50, +0x54
hash_map<CString, float> variables, +0x68 zone name, +0x6C. Create attaches both managers to the party (slot 13), so
the party is the context of its items (CItem::LoadItem's slot 22 finds the party's spell manager). Clear(1, 0) after
Create re-attaches the spell manager; DestroyParty drops the four objects by count and deletes them whatever the count
(kept). The constructor copies an uninitialised stack byte into the map's three functor bytes (not reproduced).

Serialisation (party_save.cpp, A-H5): 0065F9B0 / 0065FDE0 record Load / Save, 006618C0 / 00661C10 LoadParty /
SaveParty (RET 8, the flag covers the variables), 00661300 Assign (operator=), 006613C0 CopyConstruct. Saves:
original into a scratch memory stream, bytes compared (ISAAC state restored before ours: SaveParty draws the key
for +0x50). Record Load: the original Save gives back the bytes read. LoadParty / Assign / CopyConstruct ours only
(items registered under their ids; the copy equals the source). Global 007C8344 = the party being loaded
(LoadingParty). StatsLoad / StatsSave moved to originals_units.h.

Candidates next (A-H6 left): the party's 006629D0 / 00662AC0 / 00662DD0, then by size 00667EC0, 006407E0, 00640060,
0067CEF0, 006570E0. 00661FB0 / 00661FE0 (party item resets) look simple, but no scenario is known to reach them.

Unit records (party_members.cpp, A-H6; all ours only, scenario party_records): 006628D0 AddMember (Create when no item
manager; a stack record constructed, assigned to the new entry, destroyed; name / key / prototype / extra), 006621F0
FillFromPrototype (model text, NPC stats, scales, starting armour / weapons / spells cloned; the lists come straight from the
prototype instead of through 005143E0's CStringArrays, same names in the same order), 00664950 Assign (shallow: the item list
pointers are copied), 00660500 CopyItemsFrom (party slot 24 copies with the source counts), 006620F0 RemoveMember, and three
more copies of AlignedPtrArray::SetAtGrow (00664490 / 006645D0 / 00664710). Kept: AddMember assigns the stack record's
uninitialised statistics to the new record (BuildNpcStats then overwrites them).

## Item texts (004E0000-004E15C0; item_db_text, scenario items)

The range holds item names / descriptions from the text resources and the text manager's archive list.
(004E1770-004E5000 is CVisibilityFog and rain/snow, 004E5E50-004F0000 the DirectDraw device: other areas.)
The item databases are filled elsewhere (00456440 Rebirth-patched, 0057CB50, 0057DEE0, readers around
006995E0-0069A780). The "can't parse color/percentage" strings at 004A0xxx belong to a 3DS material chunk
reader (004A0BA0, 004A09E0), not item code; not rewritten (reads through a CFile).

**CTextManager** (`*(CTextManager**)0x007AF788`): `+0x1C` STLport list of text archives (node: next, prev,
`+0x08` stlp::string name, `+0x14` CMappedResource*), `+0x20` read cursor, `+0x24` start of the current text
(null: none), `+0x28` its size. A lookup asks every archive's GetFile (vtable slot 5) in turn; the first hit
becomes the current text. The line reader (004DF8D0, 004DFB80) is `text_reader.cpp` (script.md).

**Text keys** (record name with ' ' replaced by '_' (004DC400), upper-cased; material = material record +0x00, others = record +0x04):
0x3004 `weapon NAME MATERIAL` (kind text item_weapon), 0x3005 `armor NAME MATERIAL` (item_armor_upper for type 0-2, else item_armor_lower),
0x3006 `qitem NAME` for prototype +0x10 "none" (item_potion) else `qitem NAME MATERIAL` (category 5 only: item_wand, name + ": " + spell name,
"Unknown" without text), 0x3007 by +0x4C: 0 `litem NAME` (table 007C8778, item_loot), 1 `material MATERIAL` (+0x54, item_material), 2 by +0x50:
0-2 `instr NAME` (weapon / armor / quick record +0x54: item_weapon_shape, item_armor_shape_upper / _lower, item_wand_shape), 3 `spell ` + spell
record +0x04 CString (item_prototype), 4 `modifier ` + modifier record +0x04 (inline code; item_modifier); 0x3008 `spell ` + its CSpell's
prototype +0x04 (item_spell); 0x3009 `questitem NAME` (table 007C8758, item_quest). The kind text is looked up as `string <kind key>`; the display
name is `format_item1` (kind, name) or `format_item2` (kind, name, count) when the item's +0x48 stack count is above 1.

| Address | Name | Verify |
|---|---|---|
| 004E0000 | CTextManager::IsPastEnd | pure |
| 004E0020, 004E0150, 004E13E0 | GetString, GetItemTexts, FormatItemName | original into scratch CStrings aliased like the real outputs (0063CEE0 and 0063E820 pass one CString as both description and kind), cursor state restored; result, text, +0x20..+0x28 |
| 004E1530, 004E1540 | TextSourceList::Begin, TextSourceIterator::CopyTo | output only |
| 004E1550, 004E1590 | TextSourceList::Erase, Insert | ours only: neighbour links, node on top of the 0x18 free list (0079B22C, written directly) |

Original bugs kept: GetItemTexts always returns 1 (FormatItemName's failure branch is dead); FormatItemName
reuses the description string for the format text; GetString on a missing text copies the key itself into
`out` (shown as is in the UI); Erase checks the node for null only after using it; the text manager never
closes files from CMappedResource::GetFile (reference counts only grow, twice as fast in the verify build).
Not rewritten: 004DEBC0, 004DC400, 004DE930 (add archive), 004DEAF0 (remove archive), 004E15C0 (node creation).

## Spells: rules and construction (scenario spells)

- CanAffect target flags: 1 = not hostile units only, 2 = hostile only, 4 = not humans ("unhuma"/"unhufe"),
  8 = not "unmogo", 0x10 = not orcs ("unorma"/"unorfe"), 0x20 = not "unmoli".
- Damage types (`DamageInfo`, filled by `CSpellEffect::GetDamage` 00683950 and the weapon code 0054F630):
  spells 0/3/6/0x27/0x28 deal type 3, 1/4/7/0x29 type 5, 2/5/8 type 4; a hit on several targets scales amount
  and multiplier by 4/n.

| Address | Name | Verify |
|---|---|---|
| 0068BA20, 0068BA60, 0068B700, 0068B770 (bool AL), 0068B7C0 (bool AL), 0068B4C0 (bool) | CSpell::GetPrototype, GetModifierCount, GetDamageType, IsOffensive, HasCastFlagModifier, CanAffect | pure |
| 0068B320 (slot 2), 0068B430, 0068B350 (slot 3), 0068B370 | CSpell::Clear, InitFromPrototype, ClearModifiers, CopyTo | ours, bytes snapshot and restored, original; bytes and result compared |
| 0068B410 | CSpell::Clone | ours only: id, parameters, modifiers of the copy |
| 00683950 | CSpellEffect::GetDamage | original into a scratch DamageInfo, 0x3C bytes |
| 0068D730, 0068D280 | CLight::GetIntensityAt, SetLitColors | pure (the original starts with a 4-byte CMP + JNZ, so the wrapper calls its two branches 0068D736 point light / 0068D7B4 other directly); snapshot +0x114..+0x134 |
| 006893F0, 006894B0 | SpellCodeByName, SpellModifierCodeByName (stdcall) | pure |
| 0068BD80 (slot 2), 0068BDD0 (slot 3), 0068BC00 (slot 7), 0068BE30, 0068BEC0, 0068BF60 | CSpellContainer::Reset, Shutdown, NameField, AddModifier, AllowsModifier, Build | NameField, AllowsModifier pure; Build: spell bytes +0x48..+0x8C restored, original, bytes and code string compared; the rest ours only |
| 00688EB0, 00689310 | CSpellManager::NewContainer, CreateSpell | ours only |
| 00684120 (slot 2), 00684150 (slot 3), 006841A0, 006841E0, 00684250, 006842A0 | CSpellConstructor::Reset, Detach, SetManager, BeginConstruct, EndConstruct, AddModifier | ours only: references, container with the prototype, record last in the list |

**Cast chain.** `CastSpellUnit("healing{e2;e2}", ...)` and the other text casts go through CreateSpellFromText (00689030; text before `{` is
the prototype, `;`-separated codes are modifier names), then as CreateSpell 00689310 does with records: the context's (manager +0xAC) slot 17
gives the CSpellConstructor; SetManager(this); BeginConstruct (a new container from manager slot 23, class 0x3003); AddModifier per modifier (at
most 8 in CreateSpell), accepted when AllowsModifier says so (prototype flag bytes +0x78 r, +0x79 t, +0x7A a, +0x7B e, +0x7C d; other letters
always), after which the spell is rebuilt; EndConstruct rebuilds once more; CSpell::Clone makes the result; SetManager(null). **Build
(0068BF60)** recomputes the container's spell from scratch each time (offset mapping and modifier letters a, e, m, r, d, t, fe/ff/fg/fh/fl/fo,
ic/it are in `spell_cast.cpp`); the code string is sorted (insertion sort here, introsort in the original: same bytes). CSpellEffect +0x54 is
this spell's duration (named `targetCount`; for attack spells it may be a target count), +0x58 effect (`damage`), +0x5C radius. Build's `d` / `t`
truncation and the area sqrt are small naked helpers (FILD qword / FMUL / `_ftol`; FLD / FDIV [00743A7C] / FSQRT); its double verify run is safe
because it empties the code string first.

Original bugs kept: Build leaves the target flags alone when the prototype kind is neither 'u' nor 't', so they
accumulate across rebuilds until Reset; a modifier whose code is not found becomes code 0, which sorts first and
empties the modifier list; BeginConstruct writes the prototype into the new container without checking the
manager returned one; the container's AddModifier reports a null record as "Invalid rune" (007959F0); CopyTo
tests the address of its modifier array for null (never true); the constructor counts references to the manager
(+4) and only decrements them.

Not rewritten: 0068D310, 0068D5A0 (CLight colour contributions: return a 4-float colour by value whose 4th
component is uninitialised stack, cannot be compared bit for bit); 00688EC0 CreateSpellObject, 00688CD0
(factory), 00688990 / 00688B40 (manager teardown), serialisation (00683AF0..00683EB0, 0068B860, 0068B920),
constructors / destructors, 0068C820 / 0068C920 (find a spell / modifier record by code, thiscall on 007C8510
/ 007C8530), STLport helpers 0068C500..0068CD50, and the Rebirth-patched 00684550 (spell database load),
0068E640, 00691500. 00684180 / 00684280 / 006842E0 / 00684310 / 006838E0 have no callers.

## Spell effects (0067E6D0-00683950; scenarios spells, spells_cast)

`spells.txt`: healing, six buffs, lightning, arrow, acid ray, fireball, fire wall, acid fog. `spells_cast.txt`:
inv_lit (light strike), acid_column, litnwall (light wall), clairvoyence, vision_fog (names from the maps' .mob).

`CSpellEffect` (vtable 0074399C, 0xA8 bytes; world.h calls it `Projectile`) is one cast spell and embeds its
CSpell at +0x34. +0x08 world (sector map at +0x170 + 0x124), +0x0C bit 0 "caster not credited", +0x10 position,
+0x1C target, +0x20 caster. The world server ticks `Update` (0067E6D0); +0x30 is the state: 0 casting (`Start`
0067F870, 9 KB, a switch on the prototype index +0x4C >> 16), 1 missile in flight (`UpdateMissile` 00681BB0),
2 lasting (`UpdateLasting` 00682650), else it ends (footprint cleared, the three effect objects dropped, Update
returns 0). The three counted effect objects, also in the world's timed-object list: +0x90 visual (missile,
particle source, wall, lightning), +0x94 sound, +0x98 light; replacing one removes the old from the timed
objects and releases it (`Assign`; the original repeats it inline). +0xA0 is the state's timer.

Spell types: 0 / 0x28 fire arrow, 1 / 0x29 lightning, 2 acid ray, 3 fireball, 4 light strike, 5 acid column,
6 fire wall, 7 light wall, 8 poison cloud (acid_fog), 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26 buffs (the
target's slot 45 gets a 0x14-byte record: the spell's +0x1C..+0x2F with byte 1 = trunc(power x 16) and byte 0x10
= +0xA4), 0x12 fireworks, 0x14 clairvoyance, 0x15 possession, 0x17 vision fog, 0x18 healing, 0x1B link, 0x1C
teleport, 0x24 charm, 0x27 a counting area cloud.

"Power" factor (inlined about twenty times): `pow(clamp(damage / prototype effect, 1, 8), 0.33) - 0.2`, or 1
when the prototype's effect (+0x2C) compares equal to 0 (NaN included); clamps select float bits on FCOMP's C0
(NaN passes), pow is FYL2X / F2XM1 / FSCALE with 0.33 from the double at 007439A8 (naked `ClampPowCore`,
`PowerCore`). The fireball blast uses the same curve on `(r*r*pi) / prototype area`.

Hits: HitUnit (006835B0) skips units the spell may not affect (only checked with a caster) and calls CUnit slot 31 (the hit, the controller's
TakeHit); HitUnitsInRadius (00683600, fireball) and HitUnitsAlong (00683730, fire wall, every 4th tick) collect units (FindUnitsInRadius
005C0BD0 into a stack CPtrList, vtable 0073D454) and hit those IsSpellTarget (00681B80) accepts (size class set, or unit prototype +0x24 below
0.5); the wall only those within 0.5 of the vertical plane through the position along the direction (normal built with length 2, distance
compared with 1).

| Address | Name | Verify |
|---|---|---|
| 00681B80 | IsSpellTarget (fastcall) | pure |
| 006835B0, 00683600, 00683730 | HitUnit, HitUnitsInRadius, HitUnitsAlong | ours only; the effect's spell parameters +0x4C..+0x60 unchanged |
| 0067F870, 0067E6D0 | CSpellEffect::Start, Update | ours only: state 0..3 after, parameters unchanged / result 1 in states 0..2, else 0 with nothing kept |
| 00681BB0, 00682650 | UpdateMissile (2.6 KB), UpdateLasting (3.8 KB) | ours only: state 1..3 / 2..3, parameters unchanged |
| 0067EF40, 0067EFC0 | SetVisual, UpdateFootprint | ours only: visual == object; stamped flag follows `stamp` for 6, 7, 8, 0x27 |
| 0067F170, 0067F260, 0067F410, 0067F580, 0067F660, 0067F7C0 | CreateArrow (class 0x4E), CreateParticle (0x4A / 0x5A), CreateLightning (0x4D, with a target), CreateSound, CreateSoundSequence (class 99, max(count, 1) names), CreatePointLight (0x4B, flags 0x840) | ours only: class and kept pointer |
| 0067EE00, 0067EE40, 0067EE80 | ReleaseVisual / Sound / Light | ours only: pointer null |

Damage reaches the world only through these, so the scenarios' HP checks are what shows it arrives. Every sequence with more than one FP
operation before a store is a naked helper in `spell_effect_internal.h`. Rebirth-patched callee: 005C11D0 (CSectorMap::Move, the teleport).

Original bugs kept: lightning (1) builds its light at the target's slot-48 position without checking the target;
healing (0x18) and charm (0x24) call the target's class id without a null check; arrow and acid ray impacts read
the target after the hit unchecked; the light's SetVelocity in state 2 does not check the light. Charm with a
target or caster that is not a unit returns without changing state, so the effect restarts every tick. The light
strike (4) uses -7 as bolt width factor; the walls compute the normal with a z of 0 * k. Fireworks, clairvoyance
and possession keep a unit list on the stack for the whole function, emptied at the end.

Risks: scenario wolves are placed 5 to 13 units from the hero without checking terrain (AddUnitToServer on
blocked ground fails the `IsAlive` waitfor); if a missile spell needs a caster to fly, its waitfor fails.
Not rewritten: 0067AC70 (missile target), 0067C640 (CEffectShowVisibility ids), 00688510 (buff record
constructor), 005B2A00 / 005B2760 (footprint builders), 005C98C0 (AI noise), 005CBBE0, 00554450; 0067F360 (no
callers), 0067E510 / 0067E5D0 (constructor / destructor), 0067E7D0 / 0067E960 (save / load).

## Quest database load (006AE8E0, quest_database.cpp)

`QuestDatabaseLoad` (thiscall on the database 007C87A8, RET 4, bool in AL) is called by the quest registration
005810A0, so by every scenario that registers quests. Verified by running the original into a fresh database
(constructor 006AE0F0, destructor 006ADF40) and ours into the real one: the records ours appended must equal
the original's whose names the real database did not have yet (all fields, places, the six string lists as
text). A record is added unless the name index already holds it with a record; after push_back the index is
cleared and rebuilt over the whole vector (0064EE90). One briefing record is filled three times ("briefing
receive" reads its six lists inline, "reject" two inline and four by 006AE740, "complete" all six by 006AE740).
Records are raw blocks with the original layouts, containers go through the original members. Risk: no
unwinding for the raw records if a member throws.

## Textures: mmp and composed (texture_mmp, texture_compose)

| Address | Name | Verify |
|---|---|---|
| 00690430, 006928C0 | CTexture::CreateFromMmp, CTextureManager::LoadMmpExtra | ours only: loaded, surface, size, extra block |
| 006908E0 | MmpHeader::MipOffset | pure |
| 00693180 | `ComposeTexture` (RET 0x1C) | ours only: result 0 / 1; after 1 the cache file holds the request's layer bytes |

ComposeTexture: the cache file `<game folder>textures\<name>.mmp` keeps the request's layer bytes in its extra
block (header +0x48), so an unchanged request is reused (1). When `name` has no file and differs from the
fallback name, the fallback's file is tried. No layer textures deletes the file (0); else the image is drawn by
00693010 (not rewritten) and written, each level converted to 16 bits by the pixel converter 007AF06C slot 2
[out, in, kind, count] (kind 3 for 4444, 2 for 5551, 0 for 5650, else -1) and halved by slot 7. A file that
cannot be created goes to FatalError (the image block leaks, as in the original). Not rewritten: 00690970 /
00690B30 (surface lock copies); 00693970 is dead.

## Registry and ini (registry, ini_file, win_registry; every scenario)

| Address | Name | Verify |
|---|---|---|
| 00694520-00694DC0, 00695410, 00695600, 006958E0 | CFileRegistry construct / destroy / open / attach / IsValid / close, getters | ours only; getters both run, outputs into scratch objects |
| 006946D0 | CFileRegistry::FindData | pure |
| 00695B80-00697100 | CIniFile construct, readers, integer lists into aligned / MFC arrays | readers both run (ini reads repeat), scratch arrays; writers 00696E60..00697190 not wrapped (they write files) |
| 006972A0-006988A0 | CWinRegistry (one HKEY at +0): construct, Destroy / Close, Open (RET 8), SetDWord / SetInt, SetFloat (4-byte REG_BINARY), SetString(Int), GetDWord / GetInt, GetFloat, GetString(Int) (256-byte stack buffer, malloc on ERROR_MORE_DATA) | both run on scratch objects (a value written twice), results compared |

CWinRegistry names come from its debug messages; each getter / setter opens `subkey` below the key
(KEY_ALL_ACCESS; setters RegCreateKeyExA with class ""), reads or writes one value, closes it. Reached at
startup by the options loader (0045B210, options.cpp) and on exit by the writer (0045C560); the writer's wrapper
records the RegCreateKeyExA / RegSetValueExA import slots for both runs, so each setter writes twice in both and
the event lists match. SetString / GetString call their workers through the workers' addresses. Original quirks
kept: GetFloat reads into the stack slot of `subkey` (a binary value shorter than 4 bytes keeps the pointer's
remaining bytes); GetDWord's value local is uninitialised. Not rewritten: 00698E70..00698FB0 (static
initialisers zeroing five 3-dword globals before WinMain).

## Trader stock (item_stock; scenario trader_stock)

`ItemStock` lists (0xB8 bytes at ClassTemplate +0x11C "allowed" and +0x1D4 "magic"; `baseLevels` +0x78,
`scrollLevels` +0x98, copied from 004A6DC0's arguments by SetLevels).

**Reaching it.** The camp's Create (0060C870; the tail Ghidra splits as 0060DD2E) calls 004A6DF0 only when the
camp has a constructor record (+0x198 "constr.current" > 0, found with ClassTemplatesFind 004A74F0) whose +0x38
flag is set (by the template's constructor and again by quest completion 004A88E0); the flag is cleared after
generating. In the game a base's dialogue answer "constrN" (006064E0) sets "constr.current" = N (00578560) and
asks for screen 5; the harness step `screen 5 <N>` does the same from zone1 (fails when no template N exists).
A new game has templates 1, 3, 4, 5 ("b.smith.constr.1", "b.Shopper.constr.3", "b.kuzn.constr.4",
"b.golem.constr.5", 005800C0); the scenario opens the camp with each. `ui_camp` opens the camp with
"constr.current" 0 (like the global map's camp button) and never reaches the generators.
004A6DF0 (on the "allowed" stock) calls CopyMagic with the "magic" stock, ResetMaterialUnits, the item
generators, their instructions (1, 3) and the materials. 004A6F60 (on the "magic" stock, when the template's
+0xD flag is set) calls GenerateScrolls (40, 50), GenerateSpellInstructions (1, 3) and
GenerateModifierInstructions (20, 50) into a list handed to 00661DB0.

| Address | Name | Verify (ours only unless noted) |
|---|---|---|
| 0069AE20 (via 0069B570 weapons / 0069B550 armors / 0069B590 quick items) | GenerateItems (RET 0x14) | at most min(maxCount, prototypes) new items, counted, kinds 0x3004..0x3006; scratch empty; no material units below zero |
| 0069A110 | GenerateMaterials (RET 0xC) | as many new items as entries taken, counted |
| 00699810, 006995E0, 00699A40, 00699C90, 00699ED0 | Generate{Weapon,Armor,QuickItem,Spell,Modifier}Instructions (RET 0xC) | one new item per record in order, +0x54 the record's index, +0x48 in [min, max]; "scroll" quick items skipped |
| 0069A480, 0069A780, 0069AC20 | FillSpellMaps, GenerateSpells, GenerateScrolls | recipe map holds exactly the qualifying recipes and the modifier keys a replay of the rules picks / at most one spell per recipe, each counted / every new entry a counted kind 0x3008 item |
| 0069B5B0 | LoadLists | lists only grew, new pointer records non-null and new, earlier material entries unchanged |
| 006993B0, 00699440, 00699410 | ResetMaterialUnits, CopyMagic, SetLevels | against the original (units and rand seed restored) / spells and modifiers equal `other`'s |

Non-obvious (the rest is in item_stock.cpp): the count is min + (rand() * range / 0x7FFF) % range, and more prototypes than that shuffles the
template's prototype vector itself; a material candidate is drawn with one rand() against quality q (material +0x10) and price p (+0x34): 10%
when q < 2p/7 - p/5 or q > p/5, else trunc(110 - |(q - p/7) / (p/7 - p/5)| x 100)%; instruction items get +0x54 = (record - table start) / record
size (signed); FillSpellMaps keeps one entry per modifier kind letter a, d, e, m, r, t (replacing a held one unless at least as strong; FCOMP,
TEST AH,1: less or unordered replaces) and f / i under both letters; GenerateSpells keeps the level reached in a register (the stored level is not
updated); LoadLists reuses its count and three material ints locals for every section (a failed read keeps the previous value). **Open:** LoadLists
is reached by loading a save with class templates (004A6C40); which scenario reaches it is not confirmed (world_load / ui_saveload expected).

Original bugs kept: the template's prototype and material vectors are shuffled in place; an item whose build fails
(EndCreateItem null) is dereferenced; the 004ABD00 / 00684B20 lookups insert a null entry for every unknown name;
a material entry with an unknown material is loaded with a null record. 004A6DF0, 004A6F60, 004A6DC0 are rewritten in class_template_stock.cpp (A-S4, script.md).

## HUD panel displays (ui_hud_panels; scenario ui_hud)

The unit panel shows the hero every frame, so every scenario with a battle screen reaches it. None of the
rewritten functions is on the Rebirth list (their caller 006462A0 is).

| Address | Name | Verify |
|---|---|---|
| 006477E0, 00647B70 | CHudUnitPanel::ShowHealth, ShowParts (RET 4) | side by side from the same spoiled sprites (floats within 4 ULPs; colour row +0xD0 for ShowParts) |
| 00649810 | CHudMovePanel::ToggleFlag | ours only (orders through the local view 0066AF30): flag, every selected unit's +0x438, the button's frame |
| 0064A320, 00649E20 | CHudTopPanel::ShowPage, Layout | original with tints / positions spoiled, then ours: tints, message log position / maximum / moved flag; five sprites (4 ULPs), log rect, region offsets |

Texts go through the panel surface's DrawText (005FDE20, Rebirth-patched) and Place (005E6FA0, Rebirth-patched),
both through their addresses.

A-H6: 0064A7A0 CHudTopPanel::ShowItems (`this` is the top panel, ECX; Ghidra's decompile mislabels it) builds page 1's
text: "format_money" with the local party's +0x50 when above 0, then the local player's item list (WorldPlayer +0xE0, a
CPtrList) merged by kind (slot 14) into the client world's copies (0055E940, not rewritten), one FormatItemName line each,
copies released; into the log when page 1 is up. Ours only (stateful): one line per kind plus the money line. Reached by
every battle screen setup (00635250 / 00636150; scenario ui_hud). 0064D9B0 CObjectivesWindow::DrawQuestList
(ui_objectives.cpp; via RefreshQuests 0064E8B0, scenario ui_hud's TAB): ours only, the five marks' and the cursor's
visibility against the quests' states. ShowParts reads the unit's six body part records (+0x374, 0x1C bytes: state,
durability, maximum) with label positions from tables 00742434 / 0074244C. In ToggleFlag the button frame's
third value 0.48046875 is unreachable.
