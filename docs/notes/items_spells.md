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
