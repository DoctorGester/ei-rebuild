# Inventory: library instantiations and dead code

An inventory pass, no code rewritten. It moves code that needs no port out of the "game" total that
`tools/progress.py` reports:

- `config/library.csv`: 2,225 rows added, 379,942 bytes. These are STLport and MFC template
  instantiations (plus 6 small CRT exception helpers).
- `config/dead_code.csv` (new): 431 game functions, 122,604 bytes. Nothing reaches them.
  `tools/progress.py` now excludes this set as well.

| | game functions | game bytes | rewritten |
|---|---:|---:|---|
| before | 11,060 | 2,668,738 | 1,446 fns, 480,225 bytes, 17.99% |
| after | 8,404 | 2,166,192 | 1,296 fns, 463,628 bytes, 21.40% |

`progress.py` now counts only rewrites of functions that are still counted as game code. Of the
1,446 rewrites, 150 now fall outside that total. 46 of them are rewrites of dead functions, e.g.
0040E3D0, 00694660-00697190 and 006B5170. The rest are library code. All of these hooks are
harmless.

## Library (`config/library.csv`, notes start with "inventory sweep:")

Byte sizes come from `src/hook/function_sizes.inc`, as in progress.py. Three methods were used, in
this order:

1. **Exact copies of existing anchors.** Every Ghidra function body was normalised: branch targets
   inside the function became relative labels, call targets became their library name or `EXT`,
   and `.text` addresses were masked. Data addresses stayed as they are, so the node-pool free
   lists 0079B224.. still match. A body identical to an existing library.csv row got that row's
   name. The process repeated until no new matches appeared. Three anchor kinds were left out
   because the bodies are too generic: `list<T>::end` (`*out = this->node`), `list<T>::begin` and
   `ConstructElements<8-byte>` (a plain stdcall memset).
2. **Reviewed "loose" families.** The function bodies were normalised more aggressively, keeping
   only mnemonics and registers and masking every number. Template instantiations of one member
   function then fall into one family: the code differs only in element size and offsets. I read
   the Ghidra decompilation of the representative of each of the 150 largest families (about 400
   KB of the 545 KB that sits in families). The 98 families recognised as library are listed in
   `REV` in the analysis script. Their recognisable features:
   - STLport vector: `_Vector_base(n)`, `_M_insert_overflow`, `reserve`, `_M_fill_insert`,
     `_M_allocate_and_copy`, `_M_range_insert` and `operator=`. All allocate through the node pool
     (free lists 0079B224..0079B260, chunk pointers 0079B264..0079B26C) and fall back to operator
     new above 128 bytes.
   - STLport hashtable: `resize` and the constructor read the STLport prime list
     0073B914..0073B980. `find_or_insert` hashes strings with `__stl_hash_string` (h*5+c). Also
     `clear`, `_M_new_node`, `find`, `erase`, `begin` and iterator `_M_skip_to_next`.
   - STLport `_Rb_tree`: `insert_unique` (plain and hinted), `_M_insert`, `_M_erase`, `clear`,
     `lower_bound`, `find`, the constructor and `_M_create_node`.
   - MFC CArray: `Serialize` (CArchive::WriteCount/ReadCount, an inlined SetSize, then
     CArchive::Read/Write), `SetSize` (MFC growth rule) and `SetAtGrow`.
   - MFC CList/CMap: `NewNode` and `Serialize` (both call CPlex::Create), `RemoveAll`, `~CList`.
3. **MFC collection vtables.** Template collections that are not DECLARE_DYNAMIC have a 3-slot
   vtable: [CObject::GetRuntimeClass 007005E3, scalar deleting destructor, Serialize]. There are
   179 such vtables. Where slot 2 was already identified as a CArray/CList Serialize:
   - slot 1 (the scalar deleting destructor) became library;
   - installers became library when they are at most 200 bytes, reference that one vtable and no
     other `.rdata` address, and call nothing but library code / operator delete. These are the
     constructors and destructors.
4. **Unreviewed tail families.** These are 141 functions, 14 KB. The rules: a loose family of at
   least 2 members; it uses the node pool, CPlex or CArchive counts; it has no `.rdata`
   immediates, indirect calls or vtable stores; every callee is already library; at most one
   out-of-line pool call. Their name is "stlp/MFC container template instantiation (unreviewed
   family)". I hand-checked two random samples of 12 and 11. All but 3 were library: two
   composite game destructors and one game loop, so the rule was tightened to exclude both
   patterns.

### Deliberately not marked library (candidates for later)

These look like templates but are the game's own code, or too generic to call library:

- **Aligned array template** (about 10 KB): no vtable, data at +0 with the raw block at data[-1],
  allocation is `operator new(n*size+0x24)` aligned to 32. Families 0044F540 (SetSize, 30 copies),
  004538D0 (SetAtGrow), 0044F960, 0044F1D0 and 0044F1B0 (free, 54 copies). It is one engine
  template, so port it once.

  The existing rows `CArray<LightningPoint>::SetSize` (00468C50) and
  `CArray<LightningPoint> allocate` (00468F70) are this template, not MFC CArray. I left them
  in library.csv.
- **Scalar deleting destructors of game classes** calling `CEngineObject::DestructBase`: 557
  copies of the same 30-byte body (00402D20, ...), 16.7 KB of compiler-generated code. One generic
  helper covers them all.
- **Game RefPtr helpers** inlined everywhere: `RefPtr::Destroy` copies (30, family 00413270),
  release (29, 004141D0) and assign (9, 004292F0).
- **Game vector-serialisation helper** (families 00465090 and 0053DF50). It calls FUN_004098D0 and
  checks the flag at this+0x58.
- `list<T>::end`/`begin` copies (63 functions, about 750 bytes), 6-byte string getters (213
  copies), and the families I could not identify: 004FB170, 00437AA0, 006E33F0, 006E3010,
  004A23D0, 004AB820 and 004AB720.
- Singleton instantiations: one-off STL/MFC instantiations with a unique body are not in any
  family. They remain game code.

## Dead code (`config/dead_code.csv`, generated by `tools/find_dead_code.py`)

The analysis is reachability over the whole exe (capstone, read-only on the Rebirth `game.exe`):

- **Nodes:**
  - every start in functions.csv and extra_function_sizes.csv, all kinds;
  - "pseudo nodes" (3,832) in the code Ghidra left outside every function body, split after each
    RET/JMP. Ghidra bodies are often truncated: 00431040 is 44 bytes in Ghidra but is followed by
    a static initialiser at 00431070 and the cipher at 00431090.
- **Edges:**
  - any branch, call, immediate or displacement from a node into another node;
  - raw rel32 E8/E9/0F8x scans of all of `.text`, matched against exact starts;
  - raw dwords anywhere in the file equal to a start;
  - aligned dwords in `.text` pointing into a node (switch tables);
  - fall-through into the next node when the last instruction is not RET/JMP/INT3 and is not a
    switch table;
  - vtable → slots. vtables.csv ranges were split wherever a referenced address falls inside one:
    0073CA08 and 0073D764 overran their neighbours.
- **Roots:**
  - the entry point;
  - every pointer stored in `.rdata`/`.data` outside a known vtable (function tables, MFC message
    maps, CRT initialiser tables, EH tables);
  - any dword in `starter/addon.dll`, `EI_Plugin.asi` or `mp3dec.asi` equal to a function start.
- A vtable is live only if a live node references it. A function reached through a vtable slot of
  a live class is live.
- Pointers from `.rdata`/`.data` into the middle of a function were dropped as coincidences. 373
  such roots existed, nearly all two-letter strings such as 'EN' = 0x00454E45.
- **Ghidra cross-check:** for all 772 dead functions of every kind, every Ghidra `xrefs_to` source
  lies in dead code or in a dead vtable.
- 0057BD10 is unreachable but Rebirth patches it, so it was kept out of the dead list.

Large dead groups:

- The whole 004319E0 decoder (4,787 B) and the cipher 00431090-00431300 that calls it.
- 0046F1F0 (10,397 B): a constructor reached only from dead 004E72A0, together with the vtables it
  installs.
- 00459100 and 0045A380.
- The 3DS reader 004A0BA0, 004A1220 etc.
- Quest-script builders 004BFCC0, 004C01A0, 004C06F0 and 004C09F0.
- 0040DB50, 004803E0, 004D39D0/004D53C0, the 005A8B00 group, and 006C8990/006C9560.

Another 1,729 dead pseudo-node chunks (about 120 KB) are not counted anywhere, because progress.py
only counts Ghidra functions.

Re-run `python tools/find_dead_code.py` after changing functions.csv or library.csv. It takes about
a minute and does not need Ghidra.

## What remains: live, not yet rewritten (7,108 functions, 1,702,564 bytes)

| range | game bytes | rewritten | remaining (fns) | largest remaining |
|---|---:|---:|---:|---|
| 00400000-0043FFFF | 148,108 | 20,931 | 127,177 (815) | 0042E700 (3,436), 0041BC40 (2,004), 00435A00 (1,295) |
| 00440000-0047FFFF | 169,212 | 48,569 | 120,643 (624) | 00456440 (4,832), 004492F0 (3,893), 0044CFF0 (3,327) |
| 00480000-004BFFFF | 142,184 | 72,401 | 69,783 (397) | 004A7630 (2,147), 00481660 (2,029), 004A3CA0 (1,760) |
| 004C0000-004FFFFF | 187,618 | 31,283 | 156,335 (632) | 004D41C0 (3,156), 004EA4A0 (2,855), 004E9A50 (2,632) |
| 00500000-0053FFFF | 187,000 | 53,058 | 133,942 (757) | 00501D90 (3,211), 00500B70 (2,566), 00502CA0 (2,524) |
| 00540000-0057FFFF | 204,953 | 40,251 | 164,702 (751) | 00568800 (4,459), 0056AFE0 (3,909), 00547890 (2,850) |
| 00580000-005BFFFF | 181,444 | 53,314 | 128,130 (450) | 005B43C0 (6,705), 005A7A70 (4,204), 005927C0 (3,535) |
| 005C0000-005FFFFF | 198,411 | 33,578 | 164,833 (601) | 005F1590 (4,904), 005ECA10 (3,316), 005EE760 (2,968) |
| 00600000-0063FFFF | 232,323 | 7,545 | 224,778 (561) | 0062D350 (6,721), 0060C870 (6,549), 00613EA0 (6,425) |
| 00640000-0067FFFF | 222,439 | 48,615 | 173,824 (665) | 0067F870 (8,842), 00659420 (4,310), 00641F20 (2,562) |
| 00680000-006BFFFF | 185,456 | 41,983 | 143,473 (605) | 006BFF40 (5,051), 00682650 (3,810), 006AE8E0 (3,042) |
| 006C0000-006FFFFF | 107,044 | 12,100 | 94,944 (250) | 006C68B0 (3,743), 006D7620 (3,520), 006C98F0 (2,164) |

The 00600000-0063FFFF range (UI) has the most untouched code: 225 KB, of which only 3% is
rewritten.

### Top 40 largest remaining functions

| # | address | size | # | address | size |
|---:|---|---:|---:|---|---:|
| 1 | 0067F870 | 8,842 | 21 | 006D7620 | 3,520 |
| 2 | 0062D350 | 6,721 | 22 | 0042E700 | 3,436 |
| 3 | 005B43C0 | 6,705 | 23 | 00596E70 | 3,415 |
| 4 | 0060C870 | 6,549 | 24 | 0058B750 | 3,349 |
| 5 | 00613EA0 | 6,425 | 25 | 0044CFF0 | 3,327 |
| 6 | 006BFF40 | 5,051 | 26 | 005ECA10 | 3,316 |
| 7 | 005F1590 | 4,904 | 27 | 00501D90 | 3,211 |
| 8 | 00456440 | 4,832 | 28 | 004D41C0 | 3,156 |
| 9 | 00604C50 | 4,482 | 29 | 0058EF20 | 3,136 |
| 10 | 00568800 | 4,459 | 30 | 006AE8E0 | 3,042 |
| 11 | 00659420 | 4,310 | 31 | 005EE760 | 2,968 |
| 12 | 005A7A70 | 4,204 | 32 | 005E18E0 | 2,959 |
| 13 | 0061B2C0 | 4,110 | 33 | 0061D780 | 2,911 |
| 14 | 0063A560 | 3,961 | 34 | 004EA4A0 | 2,855 |
| 15 | 0056AFE0 | 3,909 | 35 | 00547890 | 2,850 |
| 16 | 004492F0 | 3,893 | 36 | 00604130 | 2,827 |
| 17 | 00682650 | 3,810 | 37 | 0044B980 | 2,787 |
| 18 | 006C68B0 | 3,743 | 38 | 00681BB0 | 2,649 |
| 19 | 00638EF0 | 3,651 | 39 | 006064E0 | 2,645 |
| 20 | 005927C0 | 3,535 | 40 | 00623880 | 2,645 |

All 40 are still unnamed `FUN_` in functions.csv. Rebirth patches 00456440 (#8), so it can only be
hooked in the vanilla launch (rewrite-guide rule 9).
