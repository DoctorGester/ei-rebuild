# Inventory: library instantiations and dead code

No code is rewritten here. Two CSVs take code that needs no port out of the "game" total that
`tools/progress.py` reports (it counts only rewrites of functions that are still game code; hooks
on library or dead functions, e.g. 0040E3D0, 00694660-00697190, 006B5170, are harmless). Current
totals: `python tools/progress.py`. This file holds no tables of remaining bytes (they go stale).

## Library (`config/library.csv`, notes start with "inventory sweep:")

STLport and MFC template instantiations plus a few CRT exception helpers. Recognition, in order:

1. **Exact copies of existing anchors**: bodies normalised (branch targets to labels, call targets
   to library name or `EXT`, `.text` addresses masked, data addresses kept so the node-pool free
   lists still match). Left out as too generic: `list<T>::end`, `list<T>::begin`,
   `ConstructElements<8-byte>` (plain stdcall memset).
2. **Reviewed loose families** (one template member function = one family). Markers:
   - STLport vector: allocates through the node pool (free lists 0079B224..0079B260, chunk
     pointers 0079B264..0079B26C), operator new above 128 bytes.
   - STLport hashtable: `resize` and the constructor read the prime list 0073B914..0073B980;
     `find_or_insert` hashes with `__stl_hash_string` (h*5+c).
   - STLport `_Rb_tree`, MFC CArray (`Serialize`, `SetSize` with MFC growth rule, `SetAtGrow`),
     MFC CList/CMap (`NewNode`, `Serialize` both calling CPlex::Create, `RemoveAll`, `~CList`).
3. **MFC collection vtables**: a template collection that is not DECLARE_DYNAMIC has a 3-slot
   vtable [CObject::GetRuntimeClass 007005E3, scalar deleting destructor, Serialize] (179 of
   them). With slot 2 a known CArray/CList Serialize, slot 1 is library, and so is any installer of
   at most 200 bytes that references only that vtable and calls only library code / operator delete.
4. **Unreviewed tail families** (name "stlp/MFC container template instantiation (unreviewed
   family)"): 2 or more members using the node pool, CPlex or CArchive counts, no `.rdata`
   immediates, indirect calls or vtable stores, every callee already library. Composite game
   destructors and game loops looked like this in samples, so those patterns are excluded.

### Not marked library (game code or too generic; candidates)

- **Aligned array template** (about 10 KB, one engine template, port once): no vtable, data at +0
  with the raw block at data[-1], allocation `operator new(n*size+0x24)` aligned to 32. Families
  0044F540 (SetSize, 30 copies), 004538D0 (SetAtGrow), 0044F960, 0044F1D0, 0044F1B0 (free, 54
  copies). The library rows `CArray<LightningPoint>::SetSize` (00468C50) and `... allocate`
  (00468F70) are this template, not MFC CArray.
- **Scalar deleting destructors of game classes** calling `CEngineObject::DestructBase`: 557 copies
  of one 30-byte body (00402D20, ...); one generic helper covers them.
- **Game RefPtr helpers** inlined everywhere: `Destroy` (family 00413270), release (004141D0),
  assign (004292F0). **Vector-serialisation helper** (families 00465090, 0053DF50): calls
  FUN_004098D0 and checks the flag at this+0x58.
- `list<T>::end`/`begin` copies (63), 6-byte string getters (213), and the unidentified families
  004FB170, 00437AA0, 006E33F0, 006E3010, 004A23D0, 004AB820, 004AB720. One-off instantiations with
  a unique body stay game code.

## Dead code (`config/dead_code.csv`)

`tools/find_dead_code.py` (method in its docstring; capstone, no Ghidra) takes no arguments and
rewrites the CSV from scratch, so it drops rows added by hand (functions Ghidra never made,
coincidental pointers ruled out by reading). Facts not in the script:

- Ghidra bodies are often truncated: 00431040 is 44 bytes in Ghidra but is followed by a static
  initialiser at 00431070 and the cipher at 00431090; the pseudo nodes cover such tails.
- vtables.csv ranges are split where a referenced address falls inside one (0073CA08 and 0073D764
  overran their neighbours). Data pointers into the middle of a function are dropped as
  coincidences (nearly all two-letter strings such as 'EN' = 0x00454E45).
- 0057BD10 is unreachable but Rebirth patches it, so it is kept out of the dead list.
- Large dead groups: the 004319E0 decoder and the cipher 00431090-00431300; 0046F1F0 (10 KB
  constructor reached only from dead 004E72A0, with its vtables); 00459100, 0045A380; the 3DS
  reader 004A0BA0, 004A1220; quest-script builders 004BFCC0, 004C01A0, 004C06F0, 004C09F0; 0040DB50,
  004803E0, 004D39D0/004D53C0, the 005A8B00 group, 006C8990/006C9560.
- About 120 KB of dead pseudo-node chunks are counted nowhere (progress.py counts only Ghidra
  functions).

## What remains

The remaining code is spread over 004D0000-006EFFFF; 00600000-0061FFFF and 00640000-0066FFFF have
the most unrewritten bytes. Largest unrewritten functions at the time of writing: 005B43C0
(6.7 KB), 0060C870, 00613EA0, 00456440, 00604C50, 00659420, 0061B2C0, 0063A560. Rebirth patches
00456440, so it can only be hooked in the vanilla launch (rewrite-guide rule 9).
