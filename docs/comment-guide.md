# Comment guide

Comments in `src/` tell a reader what the code does and why it has to be that way. History is in git.

## What stays

- **What a function, class or member is for**, in a sentence, when the name does not already say it.
- **Why the code is written the way it is** when someone would otherwise "fix" it: an order of float operations the
  goldens depend on, a `rand()` draw that must happen even when unused, iteration order that feeds game logic, a quirk
  of the original kept on purpose ("the last cell is never checked, as in the original"), threading.
- **Byte layouts that are real**: save files, packets, resource files ("+0x10 in the save record"), field and chunk
  ids of a format.
- **Units and ranges** the types do not say: half-cell units, degrees or radians, 0..255 alpha.
- **One pointer to the notes** (`docs/notes/ai.md`) in the file header, where the notes help.
- **Rebirth's or our own changes** on top of the original, briefly, with the flag if there is one.
- **Tags tools read**: the named-data tags right after an `=` (`kX[3] = /*0074111C*/ {1, 2, 3};`,
  tools/named_data.py) and every comment of a generated file. `TODO(ID): ...` for open work.

## What goes

- game.exe addresses, ticket ids (but `TODO(ID)`), porting history ("was a shim", "now calls", "no longer"),
  calling-convention and slot bookkeeping ("RET 4", "slot 20", "EAX", "+0x134" for a named member).
- Restating the name or the code; the same comment at the declaration and the definition (keep one).
- "As the original does" with nothing after it: say the constraint or drop it.
- Lists of callers or callees, sizes nothing depends on, file pointers (`class Foo; // foo.h`), bare rulers.
- **Comments of deleted code.** Deleting a function, member, constant or declaration deletes the comment above it
  in the same edit; a comment that now describes nothing goes too. `comments.py check` counts the plainest case
  (a column-0 block followed by a namespace's `}`) as orphans, ratcheted at 0.

## How a comment reads

Present tense, about the code as it is; short (one line for most functions); names over numbers; wrapped at about 110
columns like the code (120 hard limit). A declaration's comment says what the function is for and what a caller must
know (a side effect, units, a quirk); the steps belong to the body. Example:

```cpp
    // A random speech among those whose chance is at least `roll` (one draw of the game's rand());
    // null, without drawing, when there is none. Like the original, a list whose first record has
    // chance 0 answers null too.
```

## Removal pass

Ask of each comment: does it tell something the name, the types and the code beside it do not? If not, delete it; if
partly, cut it to that part. When in doubt about a fact (a why, a quirk, a layout), keep it, shortened. A block of 8
lines or more that describes a subsystem or research goes to the area's notes under a heading, and the source keeps a
one-line pointer; move, don't lose.

## tools/comments.py

- `check [--update]`: the ratchet build.cmd runs over `src/`, shaders (`.hlsl`, `.hlsli`) included: per file, the
  counts may only go down (config/comment_baseline.csv). Workers don't update the baseline; the coordinator does
  after a merge. The columns:
  - addresses and ticket ids in comments;
  - "banned words": x87 instruction names, slot numbers, registers (`EAX`, `ECX`, ...), MFC names in game code;
    hook-era and decompiler narration ("installs in the vanilla launch", "called through its address", "verify
    wrapper", "scalar deleting destructor", "SHR by CL"); "new renderer" (it is the renderer), "inlined",
    "provisional", a size in bytes ("0x20 bytes"), "Direct3D", "DirectDraw", "old path", "game.exe", "through its
    address" and "stored as/to/in a float" (an x87 store; SSE rounds every operation);
  - "addon addresses": addon.dll's 8-hex-digit addresses (say what the code does);
  - "unnamed Rebirth patches": a line that says "Rebirth-patched" without a patch name from
    `config/rebirth_patches.csv` or "always on";
  - "never-read comments" ("never read", "nothing reads", "nobody reads", "not read"): the member goes with the
    comment; the comment on the line of a file format's `unread...` member is not counted;
  - "orphan comments" (What goes, above).
- `stats [--files] [--kinds] [PATH... | --batch ID]`: counts per area or file; `--kinds` the removal candidates
  (offset trailers, slot mentions, registers, stale claims, long blocks, rewrap savings).
- `list [PATH...]`: every comment line still holding an address or a ticket id.
- `banned [PATH...]`: the lines with a banned word.
- `apply [PATH...]`: the mechanical rewrites (address-only comments and parentheses go).
- `samecode [REV]`: fails when a file changed since REV differs in more than comments and whitespace; a comment-only
  change that passes it needs no game run.

Write comment changes by hand, file by file, after reading the whole file; no regex rewrites across files beyond
`comments.py apply`.
