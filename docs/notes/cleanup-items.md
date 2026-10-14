# Cleanup phase 1: items (cleanup plan, ticket C2i)

How the item family is declared after the cleanup (`item*.h` / `item_*.cpp`, `item.cpp`) and what is still left. The
items themselves are in `items_spells.md`; the conventions are those of `cleanup-engine-base.md` and
`cleanup-units.md`. Every rewritten address kept its hook and verify wrapper.

## Declarations

- `CItem : CEngineObject` (`kVtableAddress` 00743DA4) and its six classes, each with its own `kVtableAddress`:
  weapon 00743DFC, armour 00743E54, quick 00743EAC, quest 00743F04, loot 00743F5C, scroll 00743FB4 (the scroll's
  slots 9, 10, 13 and 14 are the quick item's). All 22 slots are known (dumped from game.exe):
  - slots 2 / 3 / 6 / 9 / 10 are overrides of CEngineObject's (`Create`, `Destroy`, `RunTick` (the recharge),
    `Load`, `Save`; Load / Save take `void*` and cast to `CStream*`);
  - slots 13-21 are new virtuals: `InitFromRecord`, `IsSameAs(CItem*)`, `CopyTo`, `GetPrice`, `ModelName`,
    `FirstListName`, `FigureName`, `PictureName`, `GetUse`. The names return `mfc::CString` by value (the slot builds
    in the caller's result).
  - Each rewritten slot is registered through a file-local `...Hook` shim calling the qualified member (rule 4).
- CItem fields: `link_`, `condition_` / `conditionMax_` (float bits), `charge_` / `chargeMax_` / `chargeRate_`,
  `weight_` (float bits, `Weight()`), `price_`, `magicLevel_`, `spell_` (a `CSpell*`), `count_`. Weapon `reach_`,
  quest item `questItemId_`.
- Quest and loot follow the game's names: 007C8778 holds the loot records and the quest items are their own table.
- `CItemConstructor` (00744020): `Create` / `Destroy` overrides (they were Reset / Detach). `CItemManager`
  (0073F25C): `Create` and `NewObject` overrides with file-local hooks.
- `item_db.h`: the database records are typed structs. It also has typed accessors (`MaterialAt`, `WeaponAt`,
  `ArmorAt`, `QuickItemAt`, `SpellAt`, `SpellModifierAt`) and `kWeaponCodes` / `kArmorCodes`. These used to be copied
  into item.cpp, item_names.cpp and item_model_names.cpp; those copies are gone.
- `item_stock.h`: the stock's vectors are `stlp::vector`. The aliases are StockPtrVector, StockMaterialVector,
  StockStringVector, SpellRecipeVector (of `SpellRecipe`) and `stock::CandidateVector`. Writes go through
  `Start()` / `Finish()` / `EndOfStorage()`.
- Only renames, hook shims, inlining of the typed accessors and register allocation in the stock helpers changed the
  object code (checked with an object disassembly diff for each group).

## Counts (item family, before / after)

| | Before | After |
|---|---|---|
| Offset access | 22 | 0 |
| VCall | 16 | 0 |
| *At views | 0 | 0 |
| `unknown_` names | 161 | 86 |
| Byte offset arithmetic | 5 | 0 |
| Per-file vector structs | 5 | 0 |
| Raw addresses | 12 | 5 |

## Left

- Unnamed fields:
  - `CItem::unknown_3C` (prototype value3C);
  - `unknown_4C` of the weapon, quick item and scroll (combat scales the weapon's);
  - the meaning of the prototype's value38 (the item's weight);
  - most of the remaining `unknown_` are database record fields (item_db.h, item_db_serialize.cpp).
- `CItem` slots 17-21: the base bodies are still class_slots.cpp's `ItemNoName` / `ItemNoUse`. Slot 16 (004622F0,
  four bytes) is not hooked.
- `IsSameAsSlot` is kept for ui_screens.cpp (another ticket's file). Callers outside the family still use VCall on
  item slots: net_client_awards.cpp (slots 4, 12, 14), ui_battle_hover.cpp and ui_trade_grid_draw.cpp.
- item_stock.cpp's `Constructor` is a forwarder view over the item constructor (EI_ORIGINAL members
  006A5C50..006A6480), so its calls still go through the game. ui_camp.cpp has another view of it,
  `ItemConstructorAt`. The `stock::` recipe accessors return `void*`.
- File-local forwarders still use the old quest / loot names, as do `ChunkArchive::SerializeLoot` /
  `SerializeQuestItems`. The comment in originals_gameplay.h on 007C8778 is stale.
- `item_db_text`'s StringAt reaches strings by byte offset. ui_item_info reads records with `At<>`.
- `EqualOrUnordered` exists twice in the family (item.cpp, item_slots.cpp) and again in the camera, net and world
  files; it belongs in include/ei/x87.h. The `FpuCopy` `__asm` block stays.
- Raw addresses remain in item_names.cpp and item_constructor.cpp.
