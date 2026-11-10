# Lua scripting: plan

Goal: the game's mission scripts as readable Lua files that mods edit (ei-convert writes them from the originals,
which keep loading), and mods' own scripts in Lua, with running scripts (coroutines included) kept in saves. Revised
2026-11-10 with the user: readable Lua over a hidden compile step, Lua 5.3 + Eris, saves ours only. Tickets L1-L9: `python tools/tickets.py list --group lua`. Engine details are in
`script.md`; Rebirth's extensions are in `rebirth.md` ("AddNewScriptCommands").

## What the current engine is

- **Language.** It has no operators: every value comes from a built-in function. There are 300 keywords in the table, and Rebirth adds 69 more, replacing 7 of the originals.
- **Control flow.**
  - Each script instance re-checks its `if(...)then(...)` rules on every tick until KillScript.
  - For / ForIf walk a group from last to first.
  - There are four waits: Sleep, WaitSegment, SleepUntil and SleepUntilIdle.
  - A wait inside a For body ends that body for the current member, so a run only ever waits at a top-level command of its
    block.
- **Saves hold the whole VM, compiled program included** (ScriptSave):
  - command blocks, definitions, globals and instances;
  - every waiting run as block id, command index, ticks, sleep start, and a copy of its locals.

  Loading never re-reads the mission file, so a save keeps running the code it was saved with.
- **Text gets compiled at runtime too.** QFinish builds quest scripts as text, and console lines are loaded into the mission
  script.

## Design

- **Front end.** The existing parser produces an IR (L1). That keeps the lexer quirks (`Run1` lexes as `Run 1`;
  Rebirth's stricter match makes `Orc1` an identifier), #define expansion, `!n!` name qualification and the parse errors.
  The Lua writer never reads script text itself. L1 starts with a survey of every script the game runs (keywords,
  waits, nesting), which sizes the rest.
- **Built-ins and data stay in C++.** Lua drives control flow and calls bindings generated from the keyword table, which go
  to the same CalcFloat / CalcObject / CalcGroup / command code by id, under readable Lua names.
  - Object references and groups are userdata over the existing types: reference counts, deleted-object dropping, and
    groups kept sorted by address.
  - Floats stay single precision. A float→double→float round trip is exact; check that NaN payloads survive, since
    assignment copies bits.
- **Readable Lua (L3).** The old engine only waits at a top-level command of a block (a wait inside a For body ends
  that member's body), so a script converts to plain code: a rule set as condition and action functions, waits as
  calls that yield (`Sleep`, `WaitSegment`, `SleepUntil`, `SleepUntilIdle`), For / ForIf as
  `ForEach(group, function(member) ... end)` keeping the original's end-the-body rule, the WorldScript as the start
  function. ei-convert writes `<zone>.lua` beside MF9's .mob TOML and the game loads it through the loose-file lookup
  (mods.md); the original .mob's script converts at load the same way. No pc state machines: a modder reads and edits
  the file.
- **Scheduler.** It keeps Script::Tick's order exactly:
  - waiting runs first, then instances, then dropping deleted objects, then timed effects, then freeing temporaries;
  - a new instance goes at the head of the instance list;
  - an instance started by a waiting run runs in the same tick, and one started by an instance's commands runs from the
    next tick;
  - Execute counts calls, which WaitSegment uses;
  - Sleep counts ticks as the original does.
- **Saves (L6).**
  - New saves persist the whole Lua state with Eris: game-script coroutines and mod coroutines alike. The bindings form the
    "permanents" table; object references and groups persist as object ids through `__persist`, as the current save writes
    them. Waits yield from Lua, never from inside a C++ call.
  - Saves are ours only: game.exe is no longer a target (own goldens), so no old-format writer.
  - Old saves (vanilla, or ours from before Lua) load: each saved run (block, index, ticks, sleep start, locals) resumes
    through a shim that enters the converted block at that command.
- **Mods (L8).**
  - `.lua` files loaded beside or instead of the mission's (the loose-file lookup; replace as MF3's records).
  - The same built-ins under Lua names, with waits as yields.
  - A sandbox without io / os / debug.
  - Errors reported like "Script error: ...".

## Decisions (user 2026-11-10)

- Readable Lua files are the scripts' modding format (MF9 waits only on L3).
- Lua 5.3 + Eris (Eris has no 5.4; Luau + Ares less proven). Vendored unmodified, pinned.
- Saves: Eris only, old saves still load.

## Verification

- **Lockstep (`luaboth`).** The old executor and Lua run side by side on the same world. Each tick compares the variables,
  run positions, instance lists, and a log of every built-in call with its arguments.
- **Coverage.** Every scenario, plus a corpus that loads each mission's scripts and runs N ticks.
- **Saves.** Round trips: old save loaded into Lua, Lua save loaded into Lua, and old save loaded into the old engine and
  compared.
- **Determinism.** The `deterministic` step and the harness's virtual clock make the runs repeatable.

## Risks

- **Timing.** Same-tick versus next-tick behaviour, and head insertion: lockstep catches it.
- **Floats.** Exactness through Lua numbers, and NaN handling.
- **Rebirth's quirks.** They have to be kept (`DistancePointPoint` reads four floats, `Random` with one argument, and the
  others in `rebirth.md`); the bindings call the same handlers, so they carry over.
- **Speed.** Rules are polled every tick, so each binding call has to stay cheap. Measure on the biggest mission.
