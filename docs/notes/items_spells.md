# Items and spells (game rules)

Code: `src/game/item_db.h`, `src/game/item.h/.cpp`, `src/game/spell.h/.cpp`.
Verify: `tests/verify/verify_item.cpp`, `tests/verify/verify_spell.cpp`.

## Data structures recovered

### Databases (`item_db.h`)

Each database is an STLport vector of fixed-size records in a global; only begin/end are read. Every
lookup checks `index < (end - begin) / stride` (signed division, unsigned compare) and gives null
otherwise. `RecordTable::At` does exactly that.

| Global     | Stride | Contents              |
|------------|--------|-----------------------|
| 007C86D8   | 0x50   | materials             |
| 007C86F8   | 0xB4   | weapons (kind 0x3004) |
| 007C8718   | 0xC8   | armors (0x3005)       |
| 007C8738   | 0x8C   | quick items (0x3006) and spell containers (0x3008, "spell_container") |
| 007C8758   | 0x7C   | loot (0x3009)         |
| 007C8778   | 0x78   | quest items (0x3007)  |
| 007C8510   | 0x84   | spells                |
| 007C8530   | 0x24   | spell modifiers       |

Fields used (provisional names; see `itemdb::field`): item prototypes `+0x00` name (char*), `+0x0C`
type, `+0x18` text-key number, `+0x34` price, `+0x6C` category (5 and 8 = carries a spell);
weapons `+0x38` attack, `+0x84`/`+0x88` unknown stats shown in the unit's attack info, `+0x90`
damage, `+0x94` damage spread, `+0x98` float[7] damage-type weights; armors `+0xB8` unknown;
materials `+0x10` price, `+0x14` attack multiplier, `+0x28` damage multiplier; spells `+0x00`
name, `+0x04` code, `+0x0C` price; spell modifiers `+0x00` name, `+0x04` two-letter code, `+0x08`
price.

Weapon stats as the unit stats code (00538960) combines them: min damage = weapon damage x
material damage, max damage = (spread + damage) x material damage, attack = weapon attack x
material attack, damage type = first non-zero weight (6 if none).

### Items (`item.h`)

Class names are the game's GetClassName strings: `CItem` (vtable 00743DA4), `CItemWeapon`
(00743DFC, class id 0x11), `CItemArmor` (00743E54, 0x12), `CItemQuick` (00743EAC, 0x13),
`CItemQuest` (00743F04, 0x14), `CItemLoot` (00743F5C, 0x15), `CItemSpell` (00743FB4, 0x16).
`CEngineObject::type_` (+0x10) is the item kind 0x3004..0x3009 and `unknown_18` the packed id
(prototype index << 16 | material index). CItem: `+0x28` charge, `+0x2C` max, `+0x30` rate,
`+0x38` price (includes the attached spell's price), `+0x44` attached CSpell. CItemWeapon:
`+0x50` damage, `+0x54` spread. CItemLoot: `+0x4C` kind (0 own price, 1 material, 2 a record),
`+0x50` record type (weapon, armor, quick, spell, modifier), `+0x54` index.

### Spells (`spell.h`)

`CSpell` (vtable 00743A94): packed id `unknown_18` = prototype index << 16; `+0x3C` flags (low 16
bits: target restrictions, 0x10000000/0x20000000 from the prototype kind 'u'/'t'); `+0x48` price;
`+0x54` int8[8] modifier indices + 1. Target flags in CanAffect: 1 = not hostile units only, 2 =
hostile only, 4 = not humans ("unhuma"/"unhufe"), 8 = not "unmogo", 0x10 = not orcs
("unorma"/"unorfe"), 0x20 = not "unmoli". Units (class id 0x50): `+0xA4` prototype name,
`+0x244`/`+0x248` relation masks, `+0x250` side.

`DamageInfo` (0x3C bytes): seven damage-type weights, `+0x24` amount, `+0x28` multiplier and
more; filled by `CSpellEffect::GetDamage` (00683950) and by the weapon code (0054F630). Spells
0/3/6/0x27/0x28 deal type 3, 1/4/7/0x29 type 5, 2/5/8 type 4; when a spell hits several targets
the amount and multiplier are scaled by 4/n.

`CLight` (vtable 00743AFC, GetClassName "CLight", class id 10; type_ 0x31 = point light):
`+0x1C` position, `+0x98` rgb, `+0x100` range squared, `+0x104` its inverse, `+0x114` two lit
colours.

## Functions

| Address  | Name                              | Verification |
|----------|-----------------------------------|--------------|
| 006A0110 | CItem::GetPrototype               | pure, side by side |
| 006A4230 | CItem::GetCategory                | pure |
| 006A0070 | CItem::Recharge (vslot 6)         | stateful: ours, restore charge, original; compare result and charge bits |
| 006A07F0 | CItemWeapon::GetType              | pure |
| 006A0770 | CItemWeapon::GetDamageTypes       | output-only: original into a scratch float[7] |
| 006A2DE0 | CItemWeapon::RollDamage           | stateful: snapshot the global ISAAC generator, ours, restore, original; compare roll bits and generator state |
| 006A06C0 | CItemWeapon::IsSameAs (vslot 14)  | pure (calls the base comparison 0069FE90 both times) |
| 006A39E0 | CItemArmor::GetType               | pure |
| 006A31A0 | CItemArmor::GetTypeGroup          | pure |
| 006A5470 | CItemLoot::GetPrice (vslot 16)    | pure (float bits) |
| 006A2B80 | WeaponType (fastcall packed id)   | pure |
| 006A2A50 | WeaponMinDamage                   | pure (float bits) |
| 006A2AE0 | WeaponMaxDamage                   | pure (float bits) |
| 006A2BD0 | WeaponAttack                      | pure (float bits) |
| 006A2C60 | WeaponDamageType                  | pure |
| 006A2D40 | WeaponStat84                      | pure (float bits) |
| 006A2D90 | WeaponStat88                      | pure (float bits) |
| 006A3200 | ArmorFieldB8 (fastcall index)     | pure |
| 0068B320 | CSpell::Clear (vslot 2, Create)   | stateful: ours, snapshot +0x30..+0x5C, restore, original; compare bytes and result |
| 0068B430 | CSpell::InitFromPrototype         | stateful: same, bytes +0x1C..+0x5C |
| 0068BA20 | CSpell::GetPrototype              | pure |
| 0068BA60 | CSpell::GetModifierCount          | pure |
| 0068B7C0 | CSpell::HasCastFlagModifier       | pure (bool, AL only) |
| 0068B700 | CSpell::GetDamageType             | pure |
| 0068B770 | CSpell::IsOffensive               | pure (bool, AL only) |
| 0068B4C0 | CSpell::CanAffect                 | pure (bool) |
| 00683950 | CSpellEffect::GetDamage           | output-only: original into a scratch DamageInfo, all 0x3C bytes compared |
| 0068D730 | CLight::GetIntensityAt            | pure, stolen 0: the original starts with a 4-byte CMP and a JNZ, so the wrapper calls the original's two branches directly (0068D736 point light, 0068D7B4 other), each a complete thiscall body ending RET 4 |
| 0068D280 | CLight::SetLitColors              | stateful: ours, snapshot +0x114..+0x134, restore, original; compare |
| 006893F0 | SpellCodeByName (stdcall)         | pure |
| 006894B0 | SpellModifierCodeByName (stdcall) | pure |

Jump tables (0068B700, 0068B770, 00683950, 006A0110, 006A4230, 006A5470) were read from the exe,
not from the decompiler.

## Forwarders added

- `CItem::IsSameAs` -> 0069FE90 (base item comparison, vslot 14 of CItem).
- `gamecrt::mbsicmp` -> 006E8767 `_mbsicmp` (added to `src/lib/crt.h/.cpp`).
- The global ISAAC generator at 007A9D40 is used through `IsaacRandom::Next` (already rewritten).

The standalone build must also provide the database globals (007C8510..007C8778) and the RNG
global.

## Original bugs kept on purpose

- Most lookups read a field of the record without checking for null (006A07F0, 006A39E0,
  006A31A0, 006A2A50.., 006A5470, 0068B7C0): an index out of range reads near address 0 and
  faults. `Field<T>()` performs the same read through the null record.
- 006A2A50/2AE0/2BD0 check the material index but multiply by the field of a null material when
  it is out of range (fault).
- 006894B0 would return the address 4 (null record + 4) for an out-of-range match (cannot happen).

## Skipped

- 0068D310, 0068D450, 0068D5A0 (CLight colour contributions): they return a 4-float colour by
  value whose fourth component is uninitialised stack, so they cannot be compared bit for bit.
- 0068BF60 (CSpellConstructor building a spell from prototype + modifiers): uses an inlined
  STLport vector<char> push_back and std::sort over the modifier list; worth doing later with the
  stlp helpers.
- 00681BB0, 00682650 (huge spell effect application), 006A0FF0, 006A1780, 006AC970, 006AD000,
  006AE8E0 (item creation, CItemConstructor, applying items to units): large, many callees.
- CString builders (006A3240, 006A3480, 006A4040, 006A4B90, 006A5820, ...: text keys like
  "item%04d", "material%04d", "spell%04d"), serialisation (Load/Save pairs), constructors and
  destructors, refcount setters (006A5C00, 006841A0, ...) and the vector accessors
  006A5950..006A5A10: not rules, left for later.
- STLport and list helpers in 0068C500..0068CD50, 006A7xxx..006B3xxx.

# Item texts (004E0000-004E15C0)

Code: `src/game/item_db_text.h/.cpp`. Verify: `tests/verify/verify_item_db.cpp`.
Scenario: `tests/scenarios/items.txt` (money, weapons, potion, rune, quest items via script).

## What lives in 004E0000-004F0000

| Range | Contents |
|---|---|
| 004E0000-004E15C0 | item names/descriptions from the text resources (below) and the text manager's archive list |
| 004E1770-004E5000 | CVisibilityFog ("VisionFog"), rain and snow ("RainSnow", nature_rain_circle.wav) |
| 004E5E50-004F0000 | Direct3D / DirectDraw device, surfaces, display modes (CDXDirectDraw, CDXDirectDrawSurface) |

Only the first part is item code. The item databases themselves are loaded elsewhere: the
loaders that fill 007C86D8.. are 00456440 (patched by Rebirth), 0057CB50 and 0057DEE0, and the
per-table readers around 006995E0-0069A780. The "can't parse color/percentage" strings at
004A0xxx belong to a 3DS material chunk reader (004A0BA0 `Read3dsMaterial`: chunks 0xA000 name,
0xA010/0xA020/0xA030 colours as 0x10 float or 0x11 byte RGB, 0xA040/0xA050 percentages,
0xA200/0xA230 maps; 004A09E0 `Read3dsPercentage`: chunk 0x30 int16 / 0x31 float), not to the
item database; not rewritten here (they read through a CFile and cannot run twice).

## Structures

- **CTextManager** (provisional name; the instance is `*(CTextManager**)0x007AF788`):
  `+0x1C` STLport list of text archives (sentinel; node: next, prev, `+0x08` stlp::string name,
  `+0x14` CMappedResource*), `+0x20` read cursor, `+0x24` start of the current text (null: none),
  `+0x28` its size (written by GetFile). A lookup asks every archive's GetFile (vtable slot 5)
  in turn; the first hit becomes the current text. `004DF8D0` reads a line from the cursor
  (when advancing, it also skips the following lines that start with "##"), `004DFB80` reads up
  to a '#'.
- **Text keys of items** (the record's name CString with ' ' replaced by '_' (004DC400) and
  upper-cased; material = material record +0x00, others = record +0x04):

| Item kind | Key | Kind text key |
|---|---|---|
| 0x3004 weapon | `weapon NAME MATERIAL` | item_weapon |
| 0x3005 armor | `armor NAME MATERIAL` | item_armor_upper (armor type 0-2) / item_armor_lower |
| 0x3006 quick, prototype +0x10 "none" | `qitem NAME` | item_potion |
| 0x3006 other | `qitem NAME MATERIAL` | category 5 only: item_wand, name + ": " + spell name ("Unknown" if the spell has no text); other categories get no kind |
| 0x3007, +0x4C = 0 | `litem NAME` (table 007C8778) | item_loot |
| 0x3007, +0x4C = 1 | `material MATERIAL` (+0x54) | item_material |
| 0x3007, +0x4C = 2, +0x50 = 0/1/2 | `instr NAME` (weapon/armor/quick record +0x54) | item_weapon_shape / item_armor_shape_upper/_lower / item_wand_shape |
| 0x3007, +0x4C = 2, +0x50 = 3 | `spell ` + spell record +0x04 CString | item_prototype |
| 0x3007, +0x4C = 2, +0x50 = 4 | `modifier ` + modifier record +0x04 (inline code) | item_modifier |
| 0x3008 spell container | `spell ` + its CSpell's prototype +0x04 (none without a spell) | item_spell |
| 0x3009 | `questitem NAME` (table 007C8758) | item_quest |

  The kind text is looked up as `string <kind key>` (GetString). The display name is
  `format_item1` (kind, name) or, when the item's +0x48 (stack count) is above 1, `format_item2`
  (kind, name, count).
- **Naming correction for item.h/item_db.h:** this code shows 0x3007 is the loot item ("litem",
  item_loot, with the CItemLoot kind at +0x4C) and its table 007C8778 (0x78) is the loot table,
  while 0x3009 is the quest item ("questitem", item_quest) with table 007C8758 (0x7C). item.h
  names them the other way round (`kItemQuest` = 0x3007, `QuestItems()` = 007C8778,
  `kItemLoot` = 0x3009, `Loot()` = 007C8758); the code is right either way since names and
  tables are swapped consistently, so they were left alone and commented where used.
- Spell record +0x04 is a CString (the text key; what SpellCodeByName returns as "code").

## Functions

| Address | Name | Verification |
|---|---|---|
| 004E0000 | CTextManager::IsPastEnd | pure, side by side |
| 004E0020 | CTextManager::GetString | original into a scratch copy of `out` (shares the buffer, so the name-is-out's-buffer case behaves the same), cursor state recorded and restored, ours into `out`; result, text and +0x20..+0x28 compared |
| 004E0150 | CTextManager::GetItemTexts | same, three scratch CStrings aliased like the real outputs (0063CEE0 and 0063E820 pass one CString as both description and kind, so the description ends up holding the kind text) |
| 004E13E0 | CTextManager::FormatItemName | same, one scratch CString |
| 004E1530 | TextSourceList::Begin | output-only: original into a scratch slot |
| 004E1540 | TextSourceIterator::CopyTo | output-only |
| 004E1550 | TextSourceList::Erase | ours only (frees): links of the neighbours, *out, the node on top of the 0x18 free list |
| 004E1590 | TextSourceList::Insert | ours only (allocates): the new node is linked before `pos` |

Renamed in Ghidra: the above, plus 004DF8D0 `CTextManager::ReadLine`, 004DFB80
`CTextManager::ReadToHash`, 004DC400 `CString_ReplaceChar`, 004DEBC0 `TextSourceList::Find`,
004A09E0 `Read3dsPercentage`, 004A0BA0 `Read3dsMaterial`.

## Forwarders added

- 004DF8D0 `CTextManager::ReadLine`, 004DFB80 `CTextManager::ReadToHash`, 004DC400 `ReplaceChar`
  (__fastcall, `from` in DL), 004DEAB0 `FreeStringStorage` (stlp::string storage, thiscall),
  004E15C0 `TextSourceList::CreateNode` (STLport node + pair copy).
- `src/lib/mfc.h/.cpp`: CString::Empty (007008FB), MakeUpper (00700EFE), TrimRight(LPCTSTR)
  (006FE08A), operator+=(const CString&) (00700D88).
- Called directly (rewritten elsewhere, none patched by Rebirth): CItem::GetPrototype (006A0110),
  CItem::GetCategory (006A4230), CSpell::GetPrototype (0068BA20), `_mbsicmp` (006E8767).
- The STLport node free list for 0x18-byte nodes at 0079B22C is written directly (the original
  inlines the deallocation).

## Original bugs and oddities kept

- Record lookups out of range give a null record whose name CString is then read (address 0 or
  4): a fault, as in the original. The armor type (+0x0C) is read the same way.
- GetItemTexts always returns 1, so FormatItemName's failure branch is dead.
- FormatItemName reuses the description string for the format text.
- GetString on a missing text copies the key name itself into `out` (shown as is in the UI).
- Erase checks the node for null only after using it.
- The text manager never closes the text files it gets from CMappedResource::GetFile, so their
  reference counts only grow. In the verify build every lookup runs twice (original and ours),
  so they grow twice as fast there; harmless since nothing closes them.

## Skipped

- 004DEBC0, 004DF8D0, 004DFB80, 004DC400, 004DE930 (add archive), 004DEAF0 (remove archive):
  the rest of the text manager, below 004E0000 (another range).
- 004E15C0 (node creation: STLport chunk allocation, string copy with exception handling).

# Spell casting and effects (00680000-006A0000)

Code: `src/game/spell_cast.cpp` (construction), `src/game/spell_effect.cpp` (effect hits); the
classes are in `spell.h`. Verify: `tests/verify/verify_spell_cast.cpp`,
`tests/verify/verify_spell_effect.cpp`. Scenario: `tests/scenarios/spells.txt` (healing, six buffs,
lightning, arrow, acid ray, fireball, fire wall, acid fog; HP checks).

Shared files touched: none. `spell.h` (this area's header) gained the classes below and named
the bytes of `CSpellEffect` that were `unknown_00` (layout unchanged).

## What lives in this range

| Range | Contents |
|---|---|
| 0067E6D0 (just below) | `CSpellEffect::Update`: by state +0x30 runs 0067F870 (0: casting, 9 KB, per spell), 00681BB0 (1: missile flight), 00682650 (2: lasting effects), else ends the effect |
| 0067F870-00683950 | the effect state machines (sounds, particles, the 00681B80 / 006835B0 / 00683600 / 00683730 hit helpers) |
| 00683950-00684060 | GetDamage (done), CArchive serialisers of the effect (00683AF0, 00683B80, 00683D40, 00683EB0) |
| 00684060-006842A0 | `CSpellConstructor` (vtable 007439C8) |
| 00684390-00686000 | `CSpellManager`'s database load (00684550, patched by Rebirth) and its STLport containers |
| 00686000-00688940 | STLport hash_map / vector instantiations of the spell tables |
| 00688940-00689570 | `CSpellManager` (vtable 00743A14): object factory 00688CD0 (0x3002 CSpell, 0x3003 CSpellContainer), CreateSpellObject 00688EC0, CreateSpellFromText 00689030, CreateSpell 00689310, the name lookups (done) |
| 0068B1F0-0068BC00 | `CSpell` (constructor 0068B1F0, Load/Save 0068B860/0068B920) |
| 0068BA80-0068CED0 | `CSpellContainer` (vtable 00743AC8, constructor 0068BA80) and std::sort of its codes (0068CA10..0068CD50) |
| 0068CF00-0068DE00 | `CLight` / `CLightManager` |
| 0068DD00-00691A00 | `CMaterial`, `CMaterialManager` (0068E640 patched), `CTexture` |
| 00691A00-00694500 | `CTextureManager` (00691500 patched), texture files |
| 00694500-00699000 | `CFileRegistry`, ini/registry helpers (`CWinRegistry`) |
| 00699000-006A0000 | item database readers ("instruction_armor", ...), item containers |

Only the spell part was taken on here; textures, registry and the item readers are other areas.

## How a cast builds its spell

`CastSpellUnit("healing{e2;e2}", ...)` (script) and the other text casts go through
CreateSpellFromText (00689030, not rewritten): the text before `{` is the prototype name, the
`;`-separated codes inside are modifier names. It then does what CreateSpell (00689310) does
with records: the context's (manager +0xAC) slot 17 gives the CSpellConstructor; SetManager(this);
BeginConstruct(prototype) (a new container from manager slot 23 with class 0x3003, prototype
at +0x1C); AddModifier for each modifier (at most 8 in CreateSpell): when the container's
AllowsModifier accepts it (the prototype's flag bytes +0x78 r, +0x79 t, +0x7A a, +0x7B e,
+0x7C d; other letters always), the record is appended to the container's list and the spell is
rebuilt; EndConstruct rebuilds once more; CSpell::Clone makes the result (CreateSpellObject(-1),
CopyTo); SetManager(null).

**Build** (0068BF60) recomputes the container's spell (+0x30) from scratch each time (offsets
below are the spell's):

- From the prototype: +0x30 <- +0x20, +0x34 <- +0x24 (range), +0x38 <- +0x34 (t), +0x3C flags
  0x10000000 / 0x20000000 for kind 'u' / 't' (left alone otherwise), +0x40 <- +0x18 (mana), +0x44
  <- +0x1C, +0x48 <- +0x0C (price), +0x4C <- +0x3C, +0x20 <- +0x38 (duration), +0x24 <- +0x2C
  (effect), +0x28 <- +0x28 (area), +0x50 <- +0x44 (level); the packed id from the prototype's
  index found again by its code (0068C820).
- Per modifier record (+0x04 two code letters, +0x08 price, +0x10 mana, +0x14 strength, +0x18
  level): `a` area += strength x prototype area, `e` effect, `m` mana, `r` range likewise (each
  then clamped to > 0, NaN kept); `d` duration += trunc(prototype duration x strength), clamped
  to >= 0; `t` +0x38 += trunc(prototype +0x34 x strength); `fe/ff/fg/fh/fl/fo` target flags 2, 1,
  8, 4, 0x20, 0x10 (see CanAffect); `ic`/`it` cast flags 0x20000 / 0x10000. Every modifier also
  adds 1 to +0x44, its level to +0x50, its mana to +0x40 and its price to +0x48, and appends its
  index + 1 to the code string.
- The area becomes a radius, sqrt(area / pi), when positive; the mana is at least the
  prototype's; the code string is sorted and its first 8 bytes are the spell's modifier list.

So the effect object's fields that GetDamage and the hit helpers read are this spell's:
CSpellEffect +0x54 duration (named `targetCount`), +0x58 effect (`damage`), +0x5C radius.

## Effect hits

`CSpellEffect` embeds its CSpell at +0x34; +0x08 is the world (its sector map at +0x170 +
0x124), +0x0C bit 0 "the caster is not credited", +0x10 the position, +0x1C the target, +0x20
the caster. HitUnit (006835B0) skips units the spell may not affect (only checked with a
caster) and calls CUnit slot 31 (the hit, the controller's TakeHit) with (hit, caster or null,
caster). HitUnitsInRadius (00683600: the fireball's explosion) and HitUnitsAlong (00683730: the
fire wall, every 4th tick) collect the units within the radius (FindUnitsInRadius into a stack
CPtrList, vtable 0073D454, blocks of 10) and hit those IsSpellTarget accepts (size class set,
or the unit prototype's +0x24 below 0.5); the wall only those within 0.5 of the vertical plane
through the position along the given direction (the normal is built with length 2 and the
distance compared with 1).

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 00681B80 | IsSpellTarget (fastcall) | pure |
| 006835B0 | CSpellEffect::HitUnit | ours only (deals damage); the effect's spell parameters (+0x4C..+0x60) unchanged |
| 00683600 | CSpellEffect::HitUnitsInRadius | same |
| 00683730 | CSpellEffect::HitUnitsAlong | same |
| 0068B350 | CSpell::ClearModifiers (slot 3) | ours, modifiers restored, original; modifier bytes compared |
| 0068B370 | CSpell::CopyTo | ours, target +0x18..+0x5C restored, original; bytes compared |
| 0068B410 | CSpell::Clone | ours only (creates a spell): the copy's id, parameters and modifiers |
| 0068BD80 | CSpellContainer::Reset (slot 2) | ours only (frees nodes): list empty, no prototype, result 1 |
| 0068BDD0 | CSpellContainer::Shutdown (slot 3) | ours only: same |
| 0068BC00 | CSpellContainer::NameField (slot 7) | pure |
| 0068BE30 | CSpellContainer::AddModifier | ours only (allocates): the record is last in the list |
| 0068BEC0 | CSpellContainer::AllowsModifier | pure (jump tables read from the exe) |
| 0068BF60 | CSpellContainer::Build | ours, spell bytes +0x48..+0x8C restored, original; spell bytes and the code string compared |
| 00688EB0 | CSpellManager::NewContainer | ours only (creates): null or a container vtable |
| 00689310 | CSpellManager::CreateSpell | ours only: the spell's packed id names the prototype |
| 00684120 | CSpellConstructor::Reset (slot 2) | ours only: both references cleared, result 1 |
| 00684150 | CSpellConstructor::Detach (slot 3) | ours only: same |
| 006841A0 | CSpellConstructor::SetManager | ours only: manager set, container released |
| 006841E0 | CSpellConstructor::BeginConstruct | ours only: a container with the prototype |
| 00684250 | CSpellConstructor::EndConstruct | ours only: 1 when manager and container exist |
| 006842A0 | CSpellConstructor::AddModifier | ours only: 0, or 1 with the record last in the list |

Build's verify double run is safe because Build empties the code string before refilling it
(the second run reuses the buffer, possibly grown by the first). The rewrites call each other
through their game addresses (`Call*` members and forwarders), so every nested call is checked.
All the above and the state-machine entry points were renamed in Ghidra.

The compiled x87 code of HitUnitsAlong and Build was read back (`dumpbin /disasm`): the
distance sum is evaluated as (dy*ny + dz*nz) + dx*nx where the original has (dz*nz + dy*ny) +
dx*nx (the same value: addition is commutative); the length 2 / sqrt(x^2 + y^2) passes its sum
through a double (exact unless the FPU runs at 64-bit precision); Build's products and sums are
single FMUL/FADD sequences stored once, as in the original. Build's `d`/`t` truncation and the
area's sqrt(area / pi) are small naked helpers repeating the original's FILD qword / FMUL /
`_ftol` and FLD / FDIV [00743A7C] / FSQRT exactly. The sort of the code string uses an insertion
sort instead of the original's std::sort (introsort): sorting chars gives the same bytes.

## Forwarders added

- Not rewritten, this range: 0068C820 / 0068C920 (find a spell / modifier record by code through
  the tables' name hash_maps; thiscall on 007C8510 / 007C8530), 0068C680 (STLport string
  reserve), 0068C560 (list<char*> insert), 00688EC0 CSpellManager::CreateSpellObject.
- `_ftol` 006E853C (through the naked helper), `game::FatalError` 00480080 (Rebirth patches it;
  already a forwarder in lib/crt).
- Called through their addresses (rewritten elsewhere, not patched): 005C0BD0
  CSectorMap::FindUnitsInRadius, 0068B4C0 CSpell::CanAffect, 00683950 CSpellEffect::GetDamage.
  Called directly: CUnit::GetPrototype (00530930).
- Virtual calls: CEngineObject slots 2, 3, 4 (Release), 5; CUnit slot 31 (the hit); CBaseManager
  slot 23 (create object); the manager context's slot 17 (spell constructor).
- The STLport node free list for 16-byte blocks at 0079B228 is written directly (the original
  inlines the deallocation of the 12-byte list nodes).
- The fatal error texts are passed by their game addresses (00795858, 0079583C, 007958A4,
  00795990, 0079595C, 0079593C, 007959F0 "Invalid rune").

## Original bugs and oddities kept

- Build leaves the target flags alone when the prototype's kind is neither 'u' nor 't', so they
  accumulate across rebuilds until the spell is cleared (Reset clears it).
- A modifier whose code is not found becomes code 0, which sorts first and empties the spell's
  modifier list (the copy stops at the first zero).
- BeginConstruct writes the prototype into the new container without checking that the manager
  returned one.
- AddModifier (container) reports a null record as "Invalid rune".
- CopyTo tests the address of its modifier array for null (never true).
- The constructor counts references to the manager (+4) and only ever decrements them, never
  releasing it.

## Skipped and why

- 0067F870 (casting, 9 KB; starts in the range below), 00681BB0 (missile flight, 2.7 KB) and
  00682650 (lasting effects, 3.9 KB): per-spell state machines that mostly start sounds,
  particles and lights (0067F260, 0067F580, 0067F7C0, 00675D50) with pow() written as
  F2XM1/FSCALE; their damage paths are the rewritten hit helpers. Next candidates, one spell
  state at a time.
- 00689030 CreateSpellFromText: CString/CStringArray parsing with STLport temporaries and EH
  frames; it calls the rewritten constructor methods, so the scenario still covers them.
- 006838E0 (hits the effect's target; no callers), 00684180 / 00684280 / 006842E0 / 00684310
  (constructor helpers without callers).
- 00684550 (spell database load) and 0068E640 / 00691500: patched by Rebirth.
- 00688EC0 CreateSpellObject (hash_map of spell sequences), 00688990 / 00688B40 / 00688D70
  (manager teardown), serialisation (00683AF0..00683EB0, 0068B860, 0068B920), constructors and
  destructors: containers and lifetime, not rules.

## Risks

- The effect hit helpers and the construction methods are checked for consistency only; the
  scenario's HP checks are what shows the damage arrives. The spell and unit names come from
  the maps and the earlier scenarios, but none of the spells was run here: if a missile spell
  needs a caster to fly, its waitfor fails (the wolf keeps its HP).
- The scenario's wolves are placed 5 to 13 units from the hero on the start map without
  checking the terrain; AddUnitToServer on blocked ground would fail the `IsAlive` waitfor.
- `CSpellEffect` +0x54 is named `targetCount` (GetDamage splits the damage by it) and is the
  spell's `d` value, called duration above; for attack spells it may be a target count. Names
  only; the behaviour is copied.

# Items, textures, registry and settings (00690000-006B0000), round 2

Code: `src/game/item_parts.cpp`, `item_constructor.cpp`, `item_names.cpp`, `spell_text.cpp`,
`texture_mmp.cpp`, `registry.h/.cpp`, `ini_file.h/.cpp`. Verify: `tests/verify/verify_item_parts.cpp`,
`verify_item_constructor.cpp`, `verify_item_names.cpp`, `verify_texture_mmp.cpp`, `verify_registry.cpp`,
`verify_ini_file.cpp`.

## Functions

| Address | Name | Verification |
|---|---|---|
| 006A0FF0 | WeaponModelParts (fastcall index, CStringArray*) | output: original into a scratch CStringArray |
| 006A1780 | AppendWeaponModelParts (fastcall packed id, vector<string>*) | output: original into an empty scratch vector |
| 006A0490 | CItem::InitFromRecord (slot 13) | item bytes +0x20..+0x47 compared |
| 006A04D0 / 006A0520 | CItem::AttachSpell / SetSpell | ours only: spell and price |
| 006A5B20, 006A5B90, 006A5BD0, 006A5C00, 006A5C20, 006A5C50, 006A5CF0, 006A5D30 | CItemConstructor destructor body, Reset, Detach, SetManager, SetItem, BeginCreateItem, EndCreateItem, Cancel | ours only, state checked |
| 006A5CC0 / 006A5CD0 / 006A5CE0 | CItemConstructor::SetMaterial / SetPrototype / SetSpellObject | object bytes compared |
| 006A5D60 | CItemConstructor::Build | ours only: result, packed id, count |
| 00689030 | CSpellManager::CreateSpellFromText | ours only: a spell with an index in range |
| 006A0840, 006A0F70, 006A2750, 006A27E0 | CItemWeapon ModelName / FirstListName / FigureName / PictureName (slots 17-20) | text compared |
| 006A3240, 006A36A0, 006A3720, 006A37B0 | the same for CItemArmor | text |
| 006A3CD0, 006A4040 / 006A45C0, 006A47B0 / 006A5820 | CItemQuick, CItemQuest, CItemSpell names | text |
| 006A4B90, 006A5030 | CItemLoot FigureName / PictureName | text |
| 00690430 | CTexture::CreateFromMmp | ours only: loaded, surface, size, extra block |
| 006908E0 | MmpHeader::MipOffset | pure |
| 006928C0 | CTextureManager::LoadMmpExtra | ours only |
| 00694520, 00694540, 006945A0, 00694610, 00694660, 006946B0 | CFileRegistry construct / destroy / open / attach / IsValid / close | ours only |
| 006946D0 | CFileRegistry::FindData | pure (pointer and scalar flag) |
| 00694880, 00694B90, 00694BE0, 00694C80, 00694D20, 00694DC0, 00695410, 00695600, 006958E0 | CFileRegistry getters | both run, outputs into scratch objects |
| 00695B80, 00695B90, 00695BF0 | CIniFile construct / with path / SetPath | ours only |
| 00695D10, 00695F00, 00695F20, 00695F60, 00695F80, 00696030, 00696090 | CIniFile readers | both run (ini reads repeat), compared |
| 006961D0, 006963F0, 00696610, 00696830, 006969F0, 00696B20, 00696C50, 00696D80 | CIniFile integer lists into aligned / MFC arrays | scratch arrays |
| 00696E60, 00696F30, 00696F50, 00696FE0, 00697070, 00697100, 00697190 | CIniFile writers | not wrapped (they write files) |

## Notes

- Build's temporary record is constructed like the game's record classes (vtables
  00744054..00744068); the extra members of the quick/quest/loot records leak as in the original.
- FP: Build reads operands into locals before multiplying so no partial result leaves the FPU
  (checked with dumpbin); products are (a*b)*c as in the original. InitFromRecord copies +0x40/+0x48
  through FLD/FSTP (inline asm) so a signalling NaN is quieted the same way. CIniFile::GetDouble
  stores atof's result as a double before returning, as the original does.
- Name builders with an unknown weapon/armor type: the original formats an uninitialised 4-byte
  stack buffer; ours uses "" (no such types in the databases).
- Shared files changed: `console.h/.cpp` (CStringArray::SetAtGrow(int, const char*) forwarder
  006FEBCE), `spell.h` (CreateSpellFromText), `texture.h` (CTexture / CTextureManager fields,
  MmpHeader, CDXSurface, AlignedBuffer), `tools/check_signatures.py` (class return by hidden pointer;
  double parameters were counted as 4 bytes because the regex held backspace characters).

## Skipped

- 006AE8E0 (quest database load), 0069A780 / 006995E0.. (trader and random loot
  generation over STLport containers and rand; 0069AE20 / 0069A110 done, see "Trader stock"), 00693180 / 00693970 (write .mmp caches to disk),
  0069B5B0 (save load), 00690970 / 00690B30 (surface lock copies), the STLport vector instantiations
  in 006A6xxx-006ACxxx, CWinRegistry (006972A0-006990E0, plain Win32 registry wrappers: next candidate).
- Rebirth-patched: 00691500, 006A67A0, 006AC970, 006AE3A0.

# Spell effect state machines (0067E6D0-00683560)

Code: `src/game/spell_effect_start.cpp` (state 0 and the effect-object helpers),
`src/game/spell_effect_update.cpp` (Update, states 1 and 2, the release helpers),
`src/game/spell_effect_internal.h` (forwarders, x87 sequences, accessors shared by both). Verify:
`tests/verify/verify_spell_effect.cpp`. Scenarios: `tests/scenarios/spells.txt` (arrow, lightning,
acid ray, fireball, fire wall, acid fog, healing, buffs) and the new `tests/scenarios/spells_cast.txt`
(inv_lit = the light strike, acid_column, litnwall = the light wall, clairvoyence, vision_fog;
spell names from the maps' .mob files).

Shared files changed: `spell.h` only (this area's header): the `CSpellEffect` bytes +0x24..+0x34
and +0x60..+0xA8 named (target point, state, speed, visual / sound / light, footprint flag, timer,
+0xA4) and the new methods declared; layout unchanged, `sizeof == 0xA8` asserted.

## The object

`CSpellEffect` (vtable 0074399C, 0xA8 bytes; world.h calls it `Projectile`) is one cast spell.
The world server ticks it through `Update` (0067E6D0); +0x30 is the state: 0 casting (`Start`),
1 missile in flight (`UpdateMissile`), 2 lasting (`UpdateLasting`), anything else ends it (the
footprint is cleared, the three effect objects are dropped, Update returns 0 and the world removes
it). Every state is a switch on the spell's prototype index (+0x4C >> 16). The effect keeps three
counted effect objects that are also in the world's timed-object list: +0x90 the visual (missile,
particle source, wall or lightning), +0x94 the sound, +0x98 the light; replacing one removes the
old one from the timed objects and releases it (`Assign` in the header; the original repeats it
inline in every helper). +0xA0 is the state's timer.

Spell types (from the sounds and the maps' spell names): 0 / 0x28 fire arrow, 1 / 0x29 lightning,
2 acid ray, 3 fireball, 4 light strike (inv_lit), 5 acid column, 6 fire wall, 7 light wall
(litnwall), 8 poison cloud (acid_fog), 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26 buffs (the
target's slot 45 gets a 0x14-byte record: the spell's +0x1C..+0x2F with byte 1 = trunc(power x 16)
and byte 0x10 = +0xA4), 0x12 fireworks, 0x14 clairvoyance, 0x15 possession, 0x17 vision fog,
0x18 healing, 0x1B link, 0x1C teleport, 0x24 charm, 0x27 a counting area cloud.

The "power" factor that scales most sizes is inlined about twenty times:
`pow(clamp(damage / prototype effect, 1, 8), 0.33) - 0.2`, or 1 when the prototype's effect
(+0x2C) compares equal to 0 (NaN included). The clamps select float bits on FCOMP's C0 (so NaN
passes through), the pow is FYL2X / F2XM1 / FSCALE with the 0.33 from the double at 007439A8. It
is one naked helper (`ClampPowCore`, `PowerCore`) with wrappers that round to float, multiply by
a constant first, or truncate with the game's `_ftol`. The fireball's blast uses the same curve on
`(r * r * pi) / prototype area`.

## Functions rewritten

| Address | Name | Verification | Scenario |
|---|---|---|---|
| 0067F870 | CSpellEffect::Start (8.8 KB) | ours only: state 0..3 after, spell parameters +0x4C..+0x60 unchanged | spells, spells_cast |
| 0067EF40 | SetVisual | ours only: visual == object | spells_cast (light strike, light wall, vision fog) |
| 0067EFC0 | UpdateFootprint | ours only: the stamped flag follows `stamp` for 6, 7, 8, 0x27 | spells (fire wall, acid fog), spells_cast |
| 0067F170 | CreateArrow | ours only: visual is class 0x4E with the type | spells (arrow, acid ray) |
| 0067F260 | CreateParticle | ours only: class 0x4A / 0x5A, type, kept as visual | all |
| 0067F410 | CreateLightning | ours only: visual class 0x4D with a target | spells (lightning) |
| 0067F580 | CreateSound | ours only: kept sound has the class and name | all |
| 0067F660 | CreateSoundSequence | ours only: class 99, max(count, 1) names | spells (fireball, fire wall, acid fog), spells_cast |
| 0067F7C0 | CreatePointLight | ours only: class 0x4B, flags 0x840 | all |
| 0067E6D0 | CSpellEffect::Update | ours only: result 1 while in states 0..2, else 0 with nothing kept | all |
| 00681BB0 | UpdateMissile (2.6 KB) | ours only: state 1..3, parameters unchanged | spells (arrow, acid ray, fireball) |
| 00682650 | UpdateLasting (3.8 KB) | ours only: state 2..3, parameters unchanged | spells, spells_cast |
| 0067EE00 / 0067EE40 / 0067EE80 | ReleaseVisual / ReleaseSound / ReleaseLight | ours only: pointer null | all |

All are stateful (they create world objects, deal damage, move units), so only ours runs. The hits
themselves were already checked (HitUnit / HitUnitsInRadius / HitUnitsAlong above); the scenarios'
HP checks show the damage arrives.

## x87 notes

Everything with more than one floating-point operation before a store is a naked helper repeating
the original's instructions (MSVC's /fp:fast may reassociate or spill otherwise): the power curve,
the heights (`HeightPlus` / `HeightMinus` / `HeightPlusProduct`: the world's slot 27 / 28 result is
used from ST0), the fireball's planar distance, flight ticks (FISTP of the rounded float) and
velocity scale, the walls' span (the normal (-d.y, d.x, 0) scaled to the radius), the light wall's
half span for the footprint (x keeps the difference unrounded before halving, y and z do not), the
area curve, `DivideMultiply` (the acid ray / fireball light rise: the quotient is scaled before
rounding, unlike the fire arrow's), `FrightLevel` (FILD / FIDIV / FMUL 250 / FADD 50 / `_ftol`) and
`MultiplyAdd` (possession's wake radius). Single operations stored to float are plain C++. The
compiled helpers were read back with dumpbin (FDIVRP encodes as DE F1 as in the original).
Constants are read from the exe's memory except the immediates the original pushes.

## Forwarders (through the game addresses)

Rewritten elsewhere: 0058E520 / 0058E5D0 (timed objects), 00675D50 (SetVelocity), 00679280,
006792E0, 00679310 (particle setters), 00679C30 (wall direction), 0067A6B0 / 0067A890 / 0067A8C0
(lightning), 0068BA20 (GetPrototype), 00683950 / 006835B0 / 00683600 / 00683730 / 00681B80 (damage
helpers), 005C0BD0 (FindUnitsInRadius), 005B7820 / 005B78F0 (AI map stamp / clear), 005B7A40
(IsPassableWithoutSelf), 0058D3D0 (WakeUnitsAround), 004B2EF0 / 004B31D0 (ScriptGroup insert),
005302F0 (CUnit::ModeSpeed), 00552780 (controller remark). Rebirth-patched: 005C11D0
(CSectorMap::Move, the teleport). Not rewritten: 0067AC70 (missile target), 0067C640
(CEffectShowVisibility ids), 00688510 (buff record constructor), 004C96A0 / 004C96F0 (CPtrList),
00551AE0 (tame), 005B2A00 / 005B2760 (footprint builders), 005C98C0 (AI noise), 005CBBE0 (AI
memory notice), 00554450 (controller re-place), `_ftol` 006E853C, MFC CString / CStringArray.

## Original bugs and oddities kept

- Lightning (1) builds its light at the target's slot-48 position without checking the target;
  healing (0x18) and charm (0x24) call the target's class id without a null check; the impact of
  the arrow and the acid ray reads the target after the hit without a check; the light's
  SetVelocity in state 2 does not check the light.
- Charm with a target or caster that is not a unit returns without changing the state, so the
  effect stays in state 0 and is started again the next tick.
- The light strike (4) uses the constant -7 as the bolt width factor; the wall cases compute the
  wall's normal with a z of 0 * k.
- Fireworks, clairvoyance and possession collect units into a list that the original keeps on the
  stack for the whole function; it is emptied at the end (the CPtrList destructor inlined).
- The CString constructed from afxEmptyString at the start of Start is never used (its destructor
  is a no-op on the shared empty data); not reproduced.

## Skipped

- 0067F360 (no callers), 0067E510 / 0067E5D0 (constructor / destructor), 0067E7D0 / 0067E960
  (save / load), 00683A90 / 00683AB0 (counted-pointer assign, reproduced inline by `Assign`).

## Quest database load (006AE8E0)

Files: `src/game/quest_database.cpp`, `tests/verify/verify_quest_database.cpp`. Shared files: none.

| Address | Name | Verify |
|---|---|---|
| 006AE8E0 | `QuestDatabaseLoad` (thiscall on the database 007C87A8, RET 4; returns bool in AL) | the original loads the same image into a fresh database (constructor 006AE0F0, destructor 006ADF40), ours into the real one; the records ours appended must equal the original's whose names the real database did not have yet (all fields, the places and the six string lists as text) |

Reached by the quest registration 005810A0 ("<name>.quest.reg" through the text manager), so by
every scenario that registers quests (new game in zone1).

The database (0x40 bytes): +0 the quests (STLport vector of 0x38-byte records), +0xC their index
(hash map by name: functors, buckets +0x10, count +0x1C), +0x20 the briefings (0x6C-byte
records), +0x2C their index. The registry image is attached (00694610; not a registry: false).

- Quest record: name ("quest" / "name"), +0xC exp, +0x10 0, +0x14 money (GetFloat, rounded to
  float), +0x18 "zone index", +0x1C "order" (GetInt 00694B90), +0x20 vector<float> of the
  "questplaces" strings "{x;y}" (TrimLeft '{', TrimRight '}', split at ';' by 004DBE40; with two
  parts atof of each is appended, push_back inlined), +0x2C a string never set. Added unless the
  index has the name with a record (find 006B0870); after push_back (006B0710) the index is cleared
  (006B0520) and rebuilt over the whole vector (0064EE90).
- One briefing record (+0 name, +0xC exp, +0x10 money, six vector<CString>: give items, remove
  items, give quests, complete quests, available zones, complete zones, +0x5C a string, +0x68 0)
  is filled three times: "briefing receive" (all six lists read inline: the first three emptied
  with erase(begin, end) 006AFB60, the others with clear 0052E010, then reserve 006B02B0 and every
  non-empty string pushed 006AF790; added like the quest, with 006B07C0 / 006B0780 / 006B0590 /
  004297A0), "briefing reject" (the two item lists inline with clear, the other four by 006AE740;
  added by 006B0600, also unless the name is there) and "briefing complete" (the name assigned as a
  C string 0041F550, all six lists by 006AE740, added by 006B0600).
- All containers go through the original members; the records are raw blocks with the original
  layouts. Strings passed by value are copied into a raw slot the callee destroys.

Risk: ours has no unwinding for the raw records if a member throws (the original's EH would
destroy them).

## Composed textures (00693180)

Files: `src/game/texture_compose.cpp`, `tests/verify/verify_texture_compose.cpp`.

| Address | Name | Verify |
|---|---|---|
| 00693180 | `ComposeTexture` (texture manager thiscall, RET 0x1C) | ours only: result 0 / 1; after 1 the cache file of the returned name holds the request's layer bytes |

Arguments: the name used (out), the skin request (`SkinRequest`: layer bytes, layer texture names,
size), the cache name, a fallback name, the mip count, the fill colour, the pixel format. The cache
file is `<game folder>textures\<name>.mmp`; its extra block (header +0x48) is the request's layer
bytes, so a file with the same bytes is reused (result 1). When `name` has no file and differs from
the fallback, the fallback's file is tried (the out name becomes the fallback; a mismatch switches
back to `name`, a missing fallback file keeps it). Otherwise: no layer textures deletes the file
(0); the image (size x size 32-bit pixels, cleared to the fill colour) is drawn by 00693010 (0 on
failure) and written: the header ("MMP", size, mip count, format fields from 00693820 / 00693850 /
006938B0 / 00693910 or inline for 0x5650 and "PNT2"/"PNT3"), the layer bytes, then each level
converted to 16 bits by the pixel converter 007AF06C (slot 2 [out, in, kind, count], kind 3 for
4444, 2 for 5551, 0 for 5650, else -1) and halved by slot 7 [out, in, width]. A file that cannot be
created goes to FatalError (the image block leaks, as in the original). Callers: 00536670 /
005370F0 (party unit skins), 00568800 (unit skins at zone load).

## Trader stock (0069A110-0069B544, ticket H2)

Files: `src/game/item_stock.h/.cpp`, `tests/verify/verify_item_stock.cpp`. Scenario:
`trader_stock` (ticket H4). The camp's Create (0060C870; the tail Ghidra splits off as 0060DD2E)
calls 004A6DF0 only when the camp has a constructor record (+0x198: "constr.current" > 0, found
with ClassTemplatesFind 004A74F0) whose +0x38 flag is set (by the template's constructor, and again
by quest completion 004A88E0); it clears the flag after generating. `ui_camp` opens the camp with
"constr.current" 0 (as the global map's camp button does), so it never reached the generators. In
the game a base's dialogue answer "constrN" (006064E0) sets "constr.current" = N (00578560) and
asks for screen 5; the harness step `screen 5 <N>` does the same from zone1 (failing when there is
no template N). A new game has templates 1, 3, 4, 5 ("b.smith.constr.1", "b.Shopper.constr.3",
"b.kuzn.constr.4", "b.golem.constr.5" in 005800C0), and the scenario opens the camp with each.

| Address | Name | Verify |
|---|---|---|
| 0069AE20 | `ItemStock::GenerateItems` (RET 0x14; via 0069B570 weapons kind 0, 0069B550 armors kind 1, 0069B590 quick items kind 2) | ours only: at most min(maxCount, prototypes) new items, each counted, kinds 0x3004..0x3006; candidate scratch empty; no material units below zero |
| 0069A110 | `ItemStock::GenerateMaterials` (RET 0xC) | ours only: as many new items as entries taken (within the clipped [min, max]), each counted; scratch empty |

The stock lists (`ItemStock`, 0xB8 bytes, ClassTemplate +0x11C / +0x1D4): +0 the template's party
object (slot 16 the item constructor, slot 18 the item manager), +4 the class bit, vectors of record
pointers at +8 armors / +0x14 weapons / +0x20 quick items / +0x38 spells / +0x44 modifiers, +0x2C
the material entries (16 bytes: record, stack count min, max, units left), +0x50 / +0x64 the spell
and modifier maps 0069A480 fills.

GenerateItems: the count is min + (rand() * range / 0x7FFF) % range (max when min >= max). With
more prototypes than that the prototype vector itself is shuffled (STLport random_shuffle on rand())
and its first `count` copied into the scratch 007C8680, else the whole vector is assigned to it.
For each one except "wand": BeginCreateItem(slot 0 of the record, -1) and the prototype; if its
material class (+0x10) is not "none", the candidates (scratch 007C8650) are the entries of that class
with at least the prototype's units (+0x4C) left. Each candidate is drawn with a chance by its
quality q (material +0x10) against the price p (+0x34): 10% when q < 2p/7 - p/5 or q > p/5, else
trunc(110 - |(q - p/7) / (p/7 - p/5)| x 100)% (one rand() per candidate, roll 1..100). Without a
pick: class "none" gets no material, no candidates cancels the item (Cancel, SetManager(null)),
otherwise the candidates are shuffled and the first taken. The taken entry loses the units. Kinds
0, 1 (and 3) then, when ISAAC % 101 < 50 and 006A6480 allows magic (level / power from prototype
and material), 0069A780 generates spells for that level in all 8 schools and one is picked at random
(ISAAC) and attached unless its +0x40 exceeds the power. The item (EndCreateItem, SetManager(null),
count + 1; category 8 quick items get a stack of 20 + rand-based 0..29) goes to the output list.

GenerateMaterials: the same count, the material entries shuffled in place / copied to 007C8640, each
a "material" prototype (class-0x15 table 007C8778, 0069D380) item of kind 0x3007 with the entry's
material and a stack size in [min, max] (constructor +0x30).

Forwarders added: the item constructor members 006A5C00 / 006A5C50 / 006A5CC0 / 006A5CD0 / 006A5CE0 /
006A5CF0 / 006A5D30 and 006A6480 (magic capacity), 006A4230 (category), 0069D380, 0069A480 /
0069A780 (spell maps / spell generation, not rewritten), the vector members 0069E3F0 / 0069D9A0 /
0069D470 / 0069C990 (pointers) and 0069ECD0 / 0069DB00 / 0069C9D0 (material entries), the list
insert 0069C3D0 and the spell list constructor 0069C440. The vectors' operator= and erase are
written out (the original inlines them).

Original bugs kept: the prototype and material vectors of the template are shuffled in place; an
item whose build fails (EndCreateItem null) is dereferenced.

### Instructions (ticket H3)

| Address | Name | Verify |
|---|---|---|
| 00699810 | `ItemStock::GenerateWeaponInstructions` (RET 0xC) | ours only: one new item per weapon record, in order, counted, +0x54 the record's index, +0x48 in [min, max]; scratch empty |
| 006995E0 | `ItemStock::GenerateArmorInstructions` (RET 0xC) | ours only: the same over the armor records |
| 00699A40 | `ItemStock::GenerateQuickItemInstructions` (RET 0xC) | ours only: the same over the quick item records, those named "scroll" skipped |

004A6DF0 calls them with (1, 3) after the three item generators and before the materials. Each
copies the stock's weapons (+0x14) / armors (+8) / quick items (+0x20) into the scratch 007C8680
(erase, then the inlined vector operator=, `AssignPointers`), finds the class-0x15 prototype
"instruction weapon" (00796370) / "instruction armor" (0079635C) / "instruction quick item"
(0079638C) with 0069D380, and for each record builds a kind-0x3007 item with that prototype and no
material; after EndCreateItem / SetManager(null) it writes +0x54 = (record - table start) / record
size (signed; tables 007C86F8 / 0xB4, 007C8718 / 200, 007C8738 / 0x8C)
and +0x48 = the RandomCount draw (one rand() per item,
none when min >= max), counts it and appends it to the list. The quick item version skips records
whose name (+8) is "scroll" (_mbscmp). Nothing is drawn for the number of instructions: every record
gets one. The scratch is emptied at the end. Scenario: `trader_stock` (the same 004A6DF0 call as the
other generators). The verify checks ours only because the functions draw from rand and create
items; both fields it compares are written unconditionally.

### Spell maps, spells and the stock's load (ticket H4)

| Address | Name | Verify |
|---|---|---|
| 0069A480 | `ItemStock::FillSpellMaps` (RET 4) | ours only: the recipe map holds exactly the qualifying recipes (counted over its buckets); each modifier key holds what a replay of the rules picks (lookups of keys ours inserted) |
| 0069A780 | `ItemStock::GenerateSpells` (RET 0xC) | ours only: the output gained at most one spell per recipe in the map, each null or counted |
| 0069B5B0 | `ItemStock::LoadLists` (RET 4) | ours only: lists only grew, new pointer records non-null and not listed before, earlier material entries unchanged |

FillSpellMaps (from GenerateItems with the weapon / armor recipe vector 007C8568 / 007C855C;
0x34-byte recipes: +0 spell name, +0xC / +0x18 required / optional modifier names, +0x24 the
repeated modifier, +0x30 class mask): clears the recipe hash_map (+0x50, 004AAEB0) and the
modifier map (+0x64, 004AB2B0); a recipe whose class bit is set and whose spell (007C851C by name,
004ABD00) is in the stock's spells (+0x38) joins the list under its name (pair 0069C1C0, insert
0069D130, list push 0069C2E0 / 0069C2F0). Each offered modifier (+0x44) by its kind letter (+4;
switch table 0069A760 / 0069A76C): a, d, e, m, r, t under the one letter, replacing the held one
unless that is at least as strong (+0x14; FCOMP, TEST AH,1: less or unordered replaces); f and i
under both letters (+4, +5), the last wins; the rest are left out. The key string is built empty
and pushed back a character at a time in the original (00425080 / 00428F10 / 0069BF10); ours
constructs it from the letters (stlp::string), same contents.

GenerateSpells (from GenerateItems with 8 equal levels and threshold 0; also 0069AC20, not
rewritten): for each recipe of the map (begin 0069C160, next bucket 0069D0E0), skipped when ISAAC
% 101 < threshold: the modifier list is emptied, the prototype found by name (00613920 on
007C8510), level = prototype +0x44, budget = levels[prototype +0x14] rounded (FISTP), cost =
prototype +0x18. Required modifiers present in the map are added (cost += +0x10, stored; level +=
+0x18; count); the recipe is dropped when budget < level or count > 8. Optional ones while count <
8: ISAAC % 101 < 50 skips one, else added when budget >= its level + level. The repeated one (+0x24),
when budget >= its level + level: added while count < 8; kind 'm' makes cost = factor (+0x14) x base
cost + cost + added (stored) and stops when not above the base cost; the level reached is kept in a
register (the stored level is not updated), the loop stops when budget < its level + reached.
00689310 (spell manager, context slot 22) builds the spell; counted, pushed to `out` (0069BE60 /
0069C410, which counts it again), released (slot 4). The modifier list's nodes go back with
0068C640, its sentinel to the node pool.

LoadLists: see the header. The count and the three material ints live in locals the original
reuses for every section (a failed read leaves the previous value; ours does the same, starting at
0). std::find (0069E350..0069E3D0) is written out; the push_backs (0069BD30 for all three item
lists, 0069BDC0, 0069BDF0) and the material overflow (0069CC20) are called. Context slots 22 and 18
are called and their results unused, as in the original. Reached by loading a save with class
templates (004A6C40); which scenario reaches it is not confirmed yet (world_load / ui_saveload
expected).

Forwarders added (all through their original addresses): 004AAEB0, 004AB2B0, 0069D130, 0069C160,
0069D0E0, 0069C680, 0069C830, 0069C2E0, 0069C2F0, 0069C1C0, 004AD640, 0069D1E0, 004ABD00,
00613920, 00689310, 0069BF70, 0068C500, 0068C560, 0068C640, 0069BE60, 0069C410 (spells), 00461190,
004AB820, 004AB720, 00613820, 0069D290, 00684B20, 0069BD30, 0069BDC0, 0069BDF0, 0069CC20 (load). None
is on the Rebirth list. An older unmerged branch (worktree-agent-a66805191c86fe11f) had versions of
these three in `trader_goods.cpp`; this port follows the disassembly and main's `ItemStock` layout
(its FPU helper for the 'm' cost is plain C++ here).

Original bugs kept: the 004ABD00 / 00684B20 lookups insert a null entry for every unknown name;
a material entry with an unknown material is loaded with a null record.

Not rewritten: 004A6DF0 / 004A6F60 / 004A6DC0 themselves (0069AC20 done in H5, below).

### Scrolls, magic instructions, material units (ticket H5)

| Address | Name | Verify |
|---|---|---|
| 0069AC20 | `ItemStock::GenerateScrolls` (RET 0xC) | ours only: every new entry of `out` a counted item of kind 0x3008 |
| 00699C90 | `ItemStock::GenerateSpellInstructions` (RET 0xC) | ours only: one counted item per offered spell, in order, +0x48 in range, +0x54 the record's index (when the record lies in 007C8510's table); scratch 007C8670 empty |
| 00699ED0 | `ItemStock::GenerateModifierInstructions` (RET 0xC) | ours only: the same over the modifiers (table 007C8530, scratch 007C8660) |
| 006993B0 | `ItemStock::ResetMaterialUnits` | against the original: the original runs first, units and rand seed restored, ours runs; units and seeds compared |
| 00699440 | `ItemStock::CopyMagic` (RET 4) | ours only (may reallocate): spells and modifiers equal `other`'s element for element |

All are reached by `trader_stock` (the camp opened with a constructor record). The camp's 004A6DF0
(on the template's "allowed" stock +0x11C) first calls CopyMagic with the "magic" stock (+0x1D4),
then ResetMaterialUnits (each material entry's units left = RandomCount(+4, +8), the same rand
formula as the other generators), then the generators of H2 / H3. 004A6F60 (on the "magic" stock,
when the template's +0xD flag is set) calls GenerateScrolls (40, 50), GenerateSpellInstructions (1, 3)
and GenerateModifierInstructions (20, 50) into a list whose items it hands to 00661DB0.

GenerateScrolls: context slots 16, 18, 22 (22's result unused); FillSpellMaps with the scroll recipe
vector 007C8550; a spell list (0069C440); up to 10 tries, each emptying the list (the holder release
0069E540 with 0 and the node free 0069CA50, written out as `ClearSpellList`, the clear part of
GenerateItems' `DestroySpellList`) and calling GenerateSpells with the levels at +0x98 and threshold
50, stopping once the list holds at least as many spells as the stock offers (+0x38; unsigned
compare). A "spell container" (00613820 on 007C8738) of kind 0x3008, no material, carrying each
listed spell, counted, into `out`; the list is then destroyed (004C17E0 per node, then the sentinel).
The two counts are not read. Ported from the old branch's `MakeTraderScrolls` (commit c4c586e) onto
main's `ItemStock`; the old version's null test of the sentinel after dereferencing it is gone.

The magic instructions follow 00699810's shape with their own scratch vectors and vector helpers
(allocate-and-copy 0069DC70 / 0069DDD0, deallocate 004AA810 / 004AA7D0; `AssignPointers` now takes
them as parameters, the item vectors' defaults unchanged): the class-0x15 prototype "prototype"
(00793924) / "rune" (007963A4), and +0x54 the index of the record found by code (0068C820 on
007C8510 with the CString at +4, /0x84; 0068C920 on 007C8530 with the inline code at +4, /0x24;
-1 when not found), written before the +0x48 count draw. Slot 22 is called and unused here too.

`ItemStock` +0x98 is now `scrollLevels` (8 floats; 00699410 from 004A6DC0 copies it and +0x78 from
its two arguments, not rewritten). Forwarders added: 0069DC70, 004AA810, 0069DDD0, 004AA7D0,
0068C820, 0068C920 (none on the Rebirth list, none in library.csv / dead_code.csv; 0069C440 /
0069C3D0 / 0069CA50 are STLport library entries called or written out as before). Not rewritten in
this range: 00699410 (two 8-dword copies), 0069B570 / 0069B550 / 0069B590 (the GenerateItems
thunks for kinds 0 / 1 / 2).

## Weapon base figure (006A0CC0)

| Address | Name | Verify |
|---|---|---|
| 006A0CC0 | `WeaponBaseFigure` (fastcall index, CString*; AL) | against the original: result and text |

In `item_names.cpp`. Reached through the weapon's 006A0F50 (index = id >> 16) from the unit effect
point 00532860 (effects, lightning; scenarios effects / spells_cast). The number is "%.2d" of +0x18;
the original's 4-byte number buffer would overrun into the name for numbers above 999 (not in the
database; the overlapping copy is not reproduced).

### Stock leftovers (ticket H6)

| Address | Name | Verify |
|---|---|---|
| 00699410 | `ItemStock::SetLevels` (RET 8; two 8-dword copies to +0x78 / +0x98) | against the original (fields compared) |
| 0069B570 / 0069B550 / 0069B590 | `GenerateWeapons` / `GenerateArmors` / `GenerateQuickItems` (RET 0xC) | ours only: valid new items, count <= min(records, max) |

The thunks call 0069AE20 with the vectors +0x14 / +8 / +0x20 and kind 0 / 1 / 2. All reached by
`trader_stock`. None is in library.csv, dead_code.csv or the Rebirth list. `unknown_78` is now
`baseLevels`. No other functions taken. Not run in the harness (central).
