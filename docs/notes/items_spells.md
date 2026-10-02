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
