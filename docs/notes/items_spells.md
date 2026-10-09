# Items, spells and related data (rules, texts, textures, registry, trader stock, party)

Layouts, record sizes, field offsets and vtable addresses are in the headers (`item_db.h`, `item.h`, `spell.h`,
`item_stock.h`, `party.h`, `texture.h`); this file holds what they do not say. Code in `src/game/`, wrappers in
`tests/verify/verify_<name>.cpp`. The HUD panel displays, portraits, top panel, objectives and save screen pieces are in
`ui.md` (sections 7 and 8); players and their message handlers in `objects.md`.

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
their addresses, so wrappers run.

## Databases and naming

Eight STLport vectors of fixed-size records (007C86D8 materials, 007C86F8 weapons, 007C8718 armors, 007C8738 quick items and
spell containers, 007C8758 / 007C8778 quest and loot items, 007C8510 spells, 007C8530 spell modifiers). A lookup checks
`index < (end - begin) / stride` (signed division, unsigned compare), else null; callers then read fields of the null
record, so an index out of range faults (kept: `Field<T>()`).

**Naming mismatch (resolved):** the game treats 0x3007 as the loot item ("litem", item_loot, CItemLoot kind at +0x4C, table
007C8778, 0x78-byte records) and 0x3009 as the quest item ("questitem", item_quest, table 007C8758, 0x7C). `item.h` /
`item_db.h` name them the other way (`kItemQuest` = 0x3007, `QuestItems()` = 007C8778, `kItemLoot` = 0x3009, `Loot()` =
007C8758). The swap is consistent, so the code is right; `LootItemRecord`'s comment and item_stock.cpp use the game's sense.

Weapon stats as the unit stats code (00538960) combines them: min damage = weapon damage x material damage, max = (spread +
damage) x material damage, attack = weapon attack x material attack, damage type = first non-zero weight (6 if none).

Record values (item_db_records.cpp; O, every run: the vectors grow while the databases load): 006A8F20 ItemRecordHead::Assign
(three strings, +0x0C, +0x14..+0x1C, +0x34..+0x4C, byte +0x68, CStringArray::Copy of +0x20; +0x50, +0x64, +0x6C not copied)
and the five kinds' uninitialized_copy / uninitialized_fill_n (__fastcall, RET 8, the second stack argument an unused tag):
weapons 006AB9C0 / 006ABB50 (vtable 00744068; own bytes +0x6C..+0x83, +0x8C..+0xB4), armour 006ABCD0 / 006ABE20 (00744064;
+0x6C..+0xB4), quick 006ABF60 / 006AC0D0 (0074405C; +0x70 and +0x80..+0x8A, the string +0x7C constructed empty, the category
not copied), the 0x7C kind 006AC240 / 006AC3B0 (00744058; +0x6C, its zone vector +0x70 left empty), the 0x78 kind 006AC520 /
006AC640 (00744054; +0x6C..+0x77). Kept: the copies drop the fields listed as not copied. Not ported (not reached while
loading: the vectors start empty): the kinds' operator= (006A8FB0 ...) and destructors (006A88F0, 006A8950, 006A89D0,
006A8A50, 006A8AE0, 006A8BD0).

Loading (item_db_load.cpp; O; startup, every scenario): `IndexedRecordTable::LoadQuickItems / LoadLoot / LoadQuestItems`
006A6E50 / 006A7110 / 006A73B0 (a stack archive: ConstructLocal, Open, the vector serialisers, Close; then each record filed
under its name with find_or_insert 00613B70 / 006A7870 / 0069E2A0, originals_library.h; Rebirth's patched 006A67A0 calls them
by address), `ChunkArchive::SerializeQuickItems / SerializeLoot / SerializeQuestItems` 006A7C20 / 006A7D20 / 006A7E10 (the
quest_records.cpp pattern; the loads call them by address), `ItemDatabase::DestructBody` 006A6570 (007C86D8; six tables last
to first: index clear + buckets, or the materials' index destructor 006A6C20; record destructor bodies 006A88F0..006A8BD0; on
exit).

## Item rules (scenario items)

| Address | Name | V |
|---|---|---|
| 006A0110, 006A4230, 006A07F0, 006A06C0, 006A39E0, 006A31A0, 006A5470 | CItem::GetPrototype, GetCategory, CItemWeapon::GetType, IsSameAs (slot 14; calls base 0069FE90), CItemArmor::GetType, GetTypeGroup, CItemLoot::GetPrice (slot 16) | P |
| 006A2B80, 006A2A50, 006A2AE0, 006A2BD0, 006A2C60, 006A2D40, 006A2D90, 006A3200 | WeaponType, MinDamage, MaxDamage, Attack, DamageType, Stat84, Stat88, ArmorFieldB8 (fastcall on a packed id / index) | P (float bits) |
| 006A0770, 006A0070, 006A2DE0 | CItemWeapon::GetDamageTypes, CItem::Recharge (slot 6), CItemWeapon::RollDamage (declared `double`, unrounded ST0; ISAAC state restored between runs) | S |
| 0069FE90, 006A30D0, 006A3C20, 006A4510, 006A4B00 | IsSameAs (slot 14) of CItem (base body), armour, quick (also scrolls), quest, loot (`item_slots.cpp`) | P |
| 006A0490, 006A0660, 006A3070, 006A3BE0, 006A44D0, 006A4AB0 | InitFromRecord (slot 13) of CItem, weapon, armour, quick (also scrolls), quest, loot | S on a scratch copy |
| 0069FCA0, 006A2F20 / 006A0540, 006A3AD0, 006A43A0, 006A49A0, 006A56E0 | Construct of CItem, armour (S on seeded scratch) / weapon, quick, quest, loot, scroll (O: a CALL within 5 bytes, no trampoline) (`item_lifetime.cpp`) | S / O |
| 0069FE20, 0069FE50 | CItem::CreateItem / DestroyItem (slots 2 / 3, every class) | O |
| 0069FD70, 006A05B0, 006A2FC0, 006A3B30, 006A4420, 006A4A00, 006A5740 / 0069FD30, 006A0590, 006A2FA0, 006A3B10, 006A4400, 006A49E0, 006A5720 | DestructBody / DeletingDestruct (slot 0), one byte-identical copy per class (each stores CItem's vtable) | O |
| 006A0270 / 006A0360; 006A2E60 / 006A2EB0, 006A3A70 / 006A3AA0, 006A3C80 / 006A3CA0, 006A4570 / 006A4590, 006A5630 / 006A5680 | Load / Save (slots 9 / 10) of CItem, weapon, armour, quick and scroll, quest, loot (`item_save.cpp`) | Save S into capture streams; Load O, then the original Save must write back the bytes read. Save: every new game's autosave; Load: world_load, ui_saveload |

Load / Save: the base writes +0x18, +0x20..+0x3C, the spell's id (-1 none; Load looks it up in the context's spell manager),
+0x48, +0x40; armour adds its two absorption rows (not layerMask_). The loot's Load reads kind_ / recordType_ through the
stack slot of its stream argument, CItem's the spell id likewise (kept). Constructors leave +0x40 (CItem) and the quick,
quest, loot and scroll fields (+0x4C..) as they are; +0x20..+0x3C start at -1.0. The link at +0x1C is a CRefCounted (16-bit
count) that Destroy deletes at 0. IsSameAs compares the base floats (+0x20..+0x2C, +0x34..+0x3C) as "equal or unordered"
(FCOMP, TEST AH,40h) and the armour's rows and masks as dwords; the subclasses' slot 13 inline CItem::InitFromRecord and add
their record fields (the quest item takes the head's +0x6C).

## Item parts, constructor and names

| Address | Name | V |
|---|---|---|
| 006A0FF0, 006A1780 | WeaponModelParts, AppendWeaponModelParts | S into a scratch array / vector |
| 006A04D0, 006A0520 | CItem::AttachSpell, SetSpell | O |
| 006A5B20-006A5D30, 006A5CC0-006A5CE0, 006A5D60 | CItemConstructor: destructor body, Reset, Detach, SetManager, SetItem, Begin/EndCreateItem, Cancel, SetMaterial / SetPrototype / SetSpellObject, Build | O |
| 006A0840, 006A0F70, 006A2750, 006A27E0 (weapon); 006A3240, 006A36A0, 006A3720, 006A37B0 (armor); 006A3CD0; 006A4040, 006A45C0; 006A47B0, 006A5820; 006A4B90, 006A5030 (loot) | ModelName / FirstListName / FigureName / PictureName (slots 17-20) per item class | S (text) |
| 006A0CC0, 006A3480 | WeaponBaseFigure, ArmorModelName | S |
| 00689030 | CSpellManager::CreateSpellFromText (spell_text.cpp) | O |

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

| Address | Name |
|---|---|
| 0069FF70, 0069FFC0 | CItem::Clone (RET 4, spell manager), CopyTo (slot 15's base body, RET 8) |
| 006A0730 / 006A3140 / 006A3C50 / 006A4540 / 006A4B50 / 006A57F0 | CItemWeapon / Armor / Quick / Quest / Loot / Spell::CopyTo (slot 15): 0 for another kind, else 1 |
| 00661DB0, 00661F30 | CParty::AddItem (RET 0xC; flags read as bytes), RemoveItem |
| 006608D0 / 00660AB0 / 00660C90 / 00660E70 and 00660A50 / 00660C30 / 00660E10 / 00660FF0 | PartyUnitRecord::AddWeapon / AddQuickItem / AddArmor / AddSpell and the four Removes (lists +0x730 / +0x740 / +0x750 / +0x760) |

Clone = the owning manager's CreateObject(kind, -1) (slot 23), then the source's slot 15; on failure the new item gets a
reference and is released. CopyTo destroys the target (slot 3), gives it +0x18 and dwords +0x20..+0x40, a CSpell::Clone
(0068B410) of the attached spell or none, count 1; the overrides call the base through its address (`CItem::CallCopyTo`). The
record adds clone with the world's spell manager (CWorld slot 22, 0055EBA0: +0x4C). The party's adds merge with a held item
that is the same (slot 14), else the party's slot 24 (0055E940: the factory makes the item, the source copies itself into it)
gives the copy; array growth is AlignedPtrArray::SetSize (00519D70). Reached by ui_camp (0060FCA0 / 0060FE40 / 0060E900),
trader_stock (004A6F60 into 00661DB0), party_records (0058FD90 / 00590C70).

## Party records and the party object (party.h)

| Address | Name | V |
|---|---|---|
| 0065F5F0, 00664880 | PartyUnitRecord::Construct, ConstructPartyUnitRecords (stdcall: zero, then construct each) | S on seeded scratch |
| 0065F740, 0065F860 | PartyUnitRecord::Destruct, ClearItems | O |
| 00663FA0, 006641D0 | PartyMemberArray::SetSize (CParty +0x2C, MFC CArray<PartyUnitRecord>, vtable 00742FF4), Serialize (= `mfc::CArray::Serialize` with a minimal `mfc::CArchive`; vtable slot only, no caller) | O |
| 00663960, 00663AD0, 00663C00, 00663D30, 00664490, 006645D0, 00664710 | byte-identical copies of AlignedPtrArray::SetSize (00519D70) and SetAtGrow | O |
| 00661050, 006A5AB0, 00683FF0 | CParty, CItemConstructor, CSpellConstructor::Construct | S on seeded scratch (the party's variable map: bucket count and emptiness) |
| 00688840 | CSpellManager::Construct (CResourceManager's, an empty map of 193 buckets at +0xBC) | O |
| 006611D0, 00661190, 006614F0, 00661640, 00661700 | CParty::DestructBody, DeletingDestruct (slot 0), CreateParty (2), DestroyParty (3), Clear (RET 8) | O |
| 0065F9B0 / 0065FDE0, 006618C0 / 00661C10, 00661300, 006613C0 | record Load / Save, LoadParty / SaveParty (RET 8; the flag covers the variables; SaveParty draws the ISAAC key for +0x50, restored between runs), Assign (operator=), CopyConstruct (`party_save.cpp`) | saves S into a scratch memory stream; record Load round-trips through Save; the rest O |
| 006628D0, 006621F0, 00664950, 00660500, 006620F0, 006625B0, 00663E40, 00663F60 / 00663A70, 006629D0 | AddMember, FillFromPrototype, Assign (shallow: item list pointers copied), CopyItemsFrom (party slot 24 copies), RemoveMember, EquipFromPrototype (a new multiplayer hero: spells / weapons released, then the NPC record's start spells, quick items, weapons, money (+0x58), experience (+0x54)), the spell list's Add, `AlignedArray::RemoveRange`, `OpenEdgeExits` (on an edge zone (kind 1) the exits' kind-0 targets get "z.<name>" = 1 when 0 / NaN; newgame login) (`party_members.cpp`) | O; party_records, ui_hero_screen (00616AC0); Add / RemoveRange have 006625B0 as only caller |
| 00662AC0, 00662DD0, 006632B0, 0065F2D0 | MpHeroRecord::WriteFile / ReadFile (0x74-byte hero records `<folder><n>.mp`, magic 0x114: a name that is not digits before its last '.' is deleted instead; the old file is moved to temp.mp around the write), SavedParties::SaveAll (folder emptied by ClearSaveFolder, every record written), SavedParties::Destruct (static destructor of 007C82A0 at exit) (`party_file.cpp`) | O; ui_hero_delete; SaveAll when the hero screen closes with heroes |

A constructed record has seven empty strings, empty lists, +0x724 0.8, +0x728 0.2, height 1.0, +0x770 2, +0x774 0, +0x778 0,
+0x77C -1, +0x780 1 (the destructor sets the same). CParty (0x70): +0x1C item manager (0xBC, CResourceManager, vtable
0073F25C), +0x20 item constructor, +0x24 spell manager (0xD4), +0x28 spell constructor (getters: slots 18, 16, 22, 17), +0x2C
records, +0x40 items, +0x50, +0x54 hash_map<CString, float> variables, +0x68 zone name, +0x6C. Create attaches both managers
to the party (slot 13), so the party is the context of its items (CItem::LoadItem's slot 22 finds the party's spell manager).
Clear(1, 0) after Create re-attaches the spell manager. Global 007C8344 = the party being loaded (LoadingParty). StatsLoad /
StatsSave are in originals_units.h. FillFromPrototype takes the lists straight from the prototype instead of through
005143E0's CStringArrays (same names, same order). Kept: DestroyParty drops the four objects by count and deletes them
whatever the count; the constructor copies an uninitialised stack byte into the map's three functor bytes (not reproduced);
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

| Address | Name | V |
|---|---|---|
| 004E0000 | CTextManager::IsPastEnd | P |
| 004E0020, 004E0150, 004E13E0 | GetString, GetItemTexts, FormatItemName | S into scratch CStrings aliased like the real outputs (0063CEE0 and 0063E820 pass one CString as both description and kind) |
| 004E1530, 004E1540, 004E1550, 004E1590 | TextSourceList::Begin, TextSourceIterator::CopyTo, Erase, Insert (node on top of the 0x18 free list 0079B22C, written directly) | O |

Kept: GetItemTexts always returns 1 (FormatItemName's failure branch is dead); FormatItemName reuses the description string
for the format text; GetString on a missing text copies the key itself into `out` (shown as is in the UI); Erase checks the
node for null only after using it; the text manager never closes files from CMappedResource::GetFile (reference counts only
grow, twice as fast in the verify build). Not rewritten: 004DEBC0, 004DC400, 004DE930 (add archive), 004DEAF0 (remove
archive), 004E15C0 (node creation).

## Spells: rules and construction (scenario spells)

- CanAffect target flags: 1 = not hostile units only, 2 = hostile only, 4 = not humans ("unhuma"/"unhufe"), 8 = not
  "unmogo", 0x10 = not orcs ("unorma"/"unorfe"), 0x20 = not "unmoli".
- Damage types (`DamageInfo`, filled by `CSpellEffect::GetDamage` 00683950 and the weapon code 0054F630): spells
  0/3/6/0x27/0x28 deal type 3, 1/4/7/0x29 type 5, 2/5/8 type 4; a hit on several targets scales amount and multiplier by 4/n.

| Address | Name | V |
|---|---|---|
| 0068BA20, 0068BA60, 0068B700, 0068B770 (AL), 0068B7C0 (AL), 0068B4C0 | CSpell::GetPrototype, GetModifierCount, GetDamageType, IsOffensive, HasCastFlagModifier, CanAffect | P |
| 0068B320 (slot 2), 0068B430, 0068B350 (slot 3), 0068B370, 0068B410 | CSpell::Clear, InitFromPrototype, ClearModifiers, CopyTo (S), Clone (O) | S |
| 00683950 | CSpellEffect::GetDamage | S into a scratch DamageInfo |
| 0068D730, 0068D280 | CLight::GetIntensityAt (the original starts with a 4-byte CMP + JNZ, so the wrapper calls its two branches 0068D736 point light / 0068D7B4 other directly), SetLitColors | P / S |
| 006893F0, 006894B0 | SpellCodeByName, SpellModifierCodeByName (stdcall) | P |
| 0068BD80 (2), 0068BDD0 (3), 0068BC00 (7), 0068BE30, 0068BEC0, 0068BF60 | CSpellContainer::Reset, Shutdown, NameField (P), AddModifier, AllowsModifier (P), Build (S: bytes +0x48..+0x8C and the code string) | O / P / S |
| 00688EB0, 00689310, 00684120 (2), 00684150 (3), 006841A0, 006841E0, 00684250, 006842A0 | CSpellManager::NewContainer, CreateSpell, CSpellConstructor::Reset, Detach, SetManager, BeginConstruct, EndConstruct, AddModifier | O |

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

Kept: Build leaves the target flags alone when the prototype kind is neither 'u' nor 't', so they accumulate across rebuilds
until Reset; a modifier whose code is not found becomes code 0, which sorts first and empties the modifier list;
BeginConstruct writes the prototype into the new container without checking the manager returned one; the container's
AddModifier reports a null record as "Invalid rune" (007959F0); CopyTo tests the address of its modifier array for null
(never true); the constructor counts references to the manager (+4) and only decrements them.

Not rewritten: 0068D310, 0068D5A0 (CLight colour contributions: a 4-float colour by value whose 4th component is
uninitialised stack, cannot be compared bit for bit); 00688EC0 CreateSpellObject, 00688CD0 (factory), 00688990 / 00688B40
(manager teardown), serialisation (00683AF0..00683EB0, 0068B860, 0068B920, the database's 00684C50 / 00684EF0 / 00684FE0),
constructors / destructors, 0068C820 / 0068C920 (find a spell / modifier record by code, thiscall on 007C8510 / 007C8530),
STLport helpers 0068C500..0068CD50, the Rebirth-patched 00684550 (spell database load), 0068E640, 00691500. No callers:
00684180 / 00684280 / 006842E0 / 00684310 / 006838E0.

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
units (FindUnitsInRadius 005C0BD0 into a stack CPtrList, vtable 0073D454) and hit those IsSpellTarget (00681B80) accepts (size
class set, or unit prototype +0x24 below 0.5); the wall only those within 0.5 of the vertical plane through the position
along the direction (normal built with length 2, distance compared with 1).

| Address | Name | V |
|---|---|---|
| 00681B80 | IsSpellTarget (fastcall) | P |
| 006835B0, 00683600, 00683730, 0067F870, 0067E6D0, 00681BB0, 00682650 | HitUnit, HitUnitsInRadius, HitUnitsAlong, CSpellEffect::Start, Update, UpdateMissile (2.6 KB), UpdateLasting (3.8 KB) | O (states, parameters +0x4C..+0x60 unchanged) |
| 0067EF40, 0067EFC0, 0067F170, 0067F260, 0067F410, 0067F580, 0067F660, 0067F7C0, 0067EE00, 0067EE40, 0067EE80 | SetVisual, UpdateFootprint (stamped flag follows `stamp` for 6, 7, 8, 0x27), CreateArrow (class 0x4E), CreateParticle (0x4A / 0x5A), CreateLightning (0x4D, with a target), CreateSound, CreateSoundSequence (class 99, max(count, 1) names), CreatePointLight (0x4B, flags 0x840), ReleaseVisual / Sound / Light | O |

Every sequence with more than one FP operation before a store is a naked helper in `spell_effect_internal.h`. Rebirth-patched
callee: 005C11D0 (CSectorMap::Move, the teleport).

Kept: lightning (1) builds its light at the target's slot-48 position without checking the target; healing (0x18) and charm
(0x24) call the target's class id without a null check; arrow and acid ray impacts read the target after the hit unchecked;
the light's SetVelocity in state 2 does not check the light. Charm with a target or caster that is not a unit returns without
changing state, so the effect restarts every tick. The light strike (4) uses -7 as bolt width factor; the walls compute the
normal with a z of 0 * k. Fireworks, clairvoyance and possession keep a unit list on the stack for the whole function, emptied
at the end. Risk: scenario wolves are placed 5 to 13 units from the hero without checking terrain (blocked ground fails the
`IsAlive` waitfor). Not rewritten: 0067AC70 (missile target), 0067C640 (CEffectShowVisibility ids), 00688510 (buff record
constructor), 005B2A00 / 005B2760 (footprint builders), 005C98C0 (AI noise), 005CBBE0, 00554450; 0067F360 (no callers),
0067E510 / 0067E5D0 (constructor / destructor), 0067E7D0 / 0067E960 (save / load).

## Quest database (006AE8E0, quest_database.cpp, quest_records.cpp)

`QuestDatabaseLoad` (thiscall on the database 007C87A8, RET 4, bool in AL) is called by the quest registration 005810A0, so
by every scenario that registers quests. Verified by running the original into a fresh database (constructor 006AE0F0,
destructor 006ADF40) and ours into the real one: the records ours appended must equal the original's whose names the real
database did not have yet. A record is added unless the name index already holds it with a record; after push_back the index
is cleared and rebuilt over the whole vector (0064EE90). One briefing record is filled three times ("briefing receive" reads
its six lists inline, "reject" two inline and four by 006AE740, "complete" all six by 006AE740). Records are raw blocks with
the original layouts, containers go through the original members. Risk: no unwinding for the raw records if a member throws.

In archives: `QuestRecord::Serialize` 006AD7D0 (0x38 bytes; places through vector<float> fill insert 006ADC10),
`QuestBriefingRecord::Serialize` 006AD900 (0x6C; list 0 through 0052DF40, lists 1-5 inline), the vectors
`ChunkArchive::SerializeQuests` / `SerializeBriefings` 006B1F20 / 006B2010 (they call the record serialisers by address; the
database's archive 006AE3A0 is Rebirth-patched). Records read twice (ours, then the original) and compared; saving and the
vectors O. Reached by saving and loading a game (ui_saveload, world_load).

## Textures: mmp and composed (texture_mmp, texture_compose)

| Address | Name | V |
|---|---|---|
| 00690430, 006928C0 | CTexture::CreateFromMmp, CTextureManager::LoadMmpExtra | O |
| 006908E0 | MmpHeader::MipOffset | P |
| 00693180 | `ComposeTexture` (RET 0x1C) | O |

ComposeTexture: the cache file `<game folder>textures\<name>.mmp` keeps the request's layer bytes in its extra block (header
+0x48), so an unchanged request is reused (1). When `name` has no file and differs from the fallback name, the fallback's
file is tried. No layer textures deletes the file (0); else the image is drawn by 00693010 (not rewritten) and written, each
level converted to 16 bits by the pixel converter 007AF06C slot 2 [out, in, kind, count] (kind 3 for 4444, 2 for 5551, 0 for
5650, else -1) and halved by slot 7. A file that cannot be created goes to FatalError (the image block leaks, as in the
original). Not rewritten: 00690970 / 00690B30 (surface lock copies); 00693970 is dead.

## Registry and ini (registry, ini_file, win_registry; every scenario)

| Address | Name | V |
|---|---|---|
| 00694520-00694DC0, 00695410, 00695600, 006958E0 | CFileRegistry construct / destroy / open / attach / IsValid / close, getters | O; getters S into scratch objects |
| 006946D0 | CFileRegistry::FindData | P |
| 00695B80-00697100 | CIniFile construct, readers, integer lists into aligned / MFC arrays | readers S (ini reads repeat); writers 00696E60..00697190 not wrapped |
| 006972A0-006988A0 | CWinRegistry (one HKEY at +0): construct, Destroy / Close, Open (RET 8), SetDWord / SetInt, SetFloat (4-byte REG_BINARY), SetString(Int), GetDWord / GetInt, GetFloat, GetString(Int) (256-byte stack buffer, malloc on ERROR_MORE_DATA) | S on scratch objects (a value written twice) |

CWinRegistry names come from its debug messages; each getter / setter opens `subkey` below the key (KEY_ALL_ACCESS; setters
RegCreateKeyExA with class ""), reads or writes one value, closes it. Reached at startup by the options loader (0045B210) and
on exit by the writer (0045C560), whose wrapper records the RegCreateKeyExA / RegSetValueExA import slots for both runs.
SetString / GetString call their workers through the workers' addresses. Kept: GetFloat reads into the stack slot of `subkey`
(a binary value shorter than 4 bytes keeps the pointer's remaining bytes); GetDWord's value local is uninitialised. Not
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

| Address | Name (O unless marked) |
|---|---|
| 0069AE20 (via 0069B570 weapons / 0069B550 armors / 0069B590 quick items), 0069A110 | GenerateItems (RET 0x14), GenerateMaterials (RET 0xC) |
| 00699810, 006995E0, 00699A40, 00699C90, 00699ED0 | Generate{Weapon,Armor,QuickItem,Spell,Modifier}Instructions (RET 0xC; "scroll" quick items skipped) |
| 0069A480, 0069A780, 0069AC20 | FillSpellMaps, GenerateSpells, GenerateScrolls |
| 0069B5B0, 0069BAC0 (S) | LoadLists, SaveLists (counts and names; a spell's code: the first 4 bytes through a CString, where the original adds an uninitialised stack byte after a 4-letter code; party_manager.cpp calls it by address; ui_saveload) |
| 006993B0 (S), 00699440, 00699410 | ResetMaterialUnits (units and rand seed restored), CopyMagic, SetLevels |

Non-obvious: the count is min + (rand() * range / 0x7FFF) % range, and more prototypes than that shuffles the template's
prototype vector itself; a material candidate is drawn with one rand() against quality q (material +0x10) and price p
(+0x34): 10% when q < 2p/7 - p/5 or q > p/5, else trunc(110 - |(q - p/7) / (p/7 - p/5)| x 100)%; instruction items get +0x54
= (record - table start) / record size (signed); FillSpellMaps keeps one entry per modifier kind letter a, d, e, m, r, t
(replacing a held one unless at least as strong; FCOMP, TEST AH,1: less or unordered replaces) and f / i under both letters;
GenerateSpells keeps the level reached in a register (the stored level is not updated); LoadLists reuses its count and three
material ints for every section (a failed read keeps the previous value). The maps 007C851C / 007C853C hold pointers into the
spell (0x84) and modifier (0x24) tables (006AC970 turns them into indices). **Open:** LoadLists is reached by loading a save
with class templates (004A6C40); which scenario reaches it is not confirmed (world_load / ui_saveload expected); record slot 0
`Kind()` (item_stock.cpp) was named from an old comment and not re-checked in Ghidra.

Kept: the template's prototype and material vectors are shuffled in place; an item whose build fails (EndCreateItem null) is
dereferenced; the 004ABD00 / 00684B20 lookups insert a null entry for every unknown name; a material entry with an unknown
material is loaded with a null record.

## What is left in 0x00640000-0x006C0000

Classification: functions.csv kind game, minus library.csv, dead_code.csv and EI_REPLACE (`python tools/progress.py` for the
numbers; the per-group byte table of the A-H13 sweep went stale and was dropped). Library additions from this range: 90 twins
of existing entries (STLport copy / fill / list / hashtable helpers, aligned buffer and counted pointer destructors), 25 quest
database container instantiations (006AF4D0..006B3E60), 25 for the item database's record vectors, the portrait slot
vector's four, 32 for the spell database (00684D60..00688470); 0069EF50 / 0069E3F0 (`vector<T*>` range insert / insert);
006B4000-006C0000 reviewed in A-H13 (sound.md). Game code that stays: the item kinds' operator= (006A8FB0 ...) and serialisers
from 006A7940 below 006A7C20, the spell serialisers. 00646C25 is a tail of 006462A0 that Ghidra splits off. Most of what is
left in 00640000-00660000 is Rebirth-patched (ui.md section 7 lists them).

**A-H19** (verify wrappers in brackets; scenarios): 0064F550 is a second copy of QuestTable::Find (004C9C90), registered on the
same method (against the original; no scenario: only the sort's median calls it, with more than 16 quests, and those must
all have records: the sort's comparison puts a quest without a record before every quest, another such one included, so with
unknown names the unguarded partition 0064F650 runs past the array in the original too; the first `ui_objectives_sort` with 17
made-up names crashed that way, X26, and was dropped); CParty::RemoveQuestItems
00661FB0 (ours, the list minus class 0x14; every new game) and ReleaseAllItems 00661FE0 (ours, empty list; only the
multiplayer exchange, no scenario); the server's login handler 0066F890 (CPlayerServer::OnLogin, ours: stream moved 0x88, the
written message and state; every new game / zone change) with its name check 0047ADA0 (IsPrintableText, against the original).
The server world's +0x144 is a CFileRegistry (`CWorldServer::Messages()`). Callers of 00661FB0 / 00661FE0 keep their forwarders
so the wrappers see them.
Interface controls (ui_scrollbar.cpp, ui_spin.cpp, ui_edit_field.cpp, ui_scroll_text.cpp): CUiScrollbar Construct 0065AEA0 /
SetPosition 0065B0C0 / SetRect 0065B490 and CUiSlider::Construct 0065BC30 (verify_ui_scrollbar.cpp: SetPosition against the
original with the thumb sprite's position, the rest ours only; objectives, save / load and options windows); CUiSpin Construct
0065BCD0, AddText 0065BE70, GetValue 0065BEB0 (against the original), SetValue 0065BEE0 (verify_ui_spin.cpp; the options window);
CUiEditField Construct 0065C010 (ours) and OnChar 0065C930 (against the original; no scenario types into an edit field: the
message box with flag 4 and the multiplayer screens); CUiScrollText::NextToken 0065D970 (against the original; every log text).
ReadStringList 006AE740 (verify_quest_lists.cpp, against the original into a scratch vector; every quest registration; the
loader keeps calling it through its address). Kept: the scroll bar's SetPosition divides by the maximum without a check (0 gives
the integer indefinite); the edit field's constructor leaves caret / editing / changed unset; NextToken's unknown BEL symbol
takes the token's data pointer as its length should FatalError return; NextToken calls GetBuffer(0) on its text.
Shared: 005FCDD0 (CUiSurface over a RECT) in originals_ui.h (string_map.cpp and ui_console_window.cpp keep their own);
CString::FindOneOf (00700ECC) moved into mfc.h from ui_console_window.cpp; gamecrt::isprint (006E944C).

**Next candidates**: the item kinds' serialisers from 006A7940 below 006A7C20; CUiScrollText's constructor 0065CC00, destructor
body 0065CE20 and line layout 0065DAA0; the tooltip's constructor 0065E320 and line drawing 0065E880 (CUiTooltip is not declared
as a CUiWindow yet); the quest database's constructor / destructor 006AE0F0 / 006ADF40 (its verify wrapper uses them) and
006AD000 / 006AD670; 0065AB80 / 0065AD00 are STLport pair<string, string> node helpers (library candidates).
