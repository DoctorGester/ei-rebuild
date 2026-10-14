# Lua scripting: plan (2026-10-14, for after the standalone exe)

Goal (user, 2026-10-14): run the game's mission scripts on Lua without changing the assets, and let mods write scripts in
Lua, with running scripts (coroutines included) kept in saves. Tickets L1-L9 in docs/tickets.md. Engine details are in
`script.md`; Rebirth's extensions are in `rebirth.md` ("AddNewScriptCommands").

## What the current engine is

- **Language.** It has no operators: every value comes from a built-in function. There are 300 keywords in the table at
  0078BCA0, and Rebirth adds 69 more, replacing 7 of the originals.
- **Control flow.**
  - Each script instance re-checks its `if(...)then(...)` rules on every tick until KillScript.
  - For / ForIf walk a group from last to first.
  - There are four waits: Sleep, WaitSegment, SleepUntil and SleepUntilIdle.
  - A wait inside a For body ends that body for the current member, so a run only ever waits at a top-level command of its
    block.
- **Saves hold the whole VM, compiled program included** (ScriptSave 004C0F90):
  - command blocks, definitions, globals and instances;
  - every waiting run as block id, command index, ticks, sleep start, and a copy of its locals.

  Loading never re-reads the mission file, so a save keeps running the code it was saved with.
- **Text gets compiled at runtime too.** QFinish builds quest scripts as text, and console lines are loaded into the mission
  script.

## Design

- **Front end.** The existing parser, rewritten, produces an IR. That keeps the lexer quirks (`Run1` lexes as `Run 1`;
  Rebirth's stricter match makes `Orc1` an identifier), #define expansion, `!n!` name qualification and the parse errors.
  The transpiler never reads script text itself.
- **Built-ins and data stay in C++.** Lua drives control flow and calls bindings generated from the keyword table, which go
  to the same CalcFloat / CalcObject / CalcGroup / command code by id.
  - Object references and groups are userdata over the existing types: reference counts, deleted-object dropping, and
    groups kept sorted by address.
  - Floats stay single precision. A float→double→float round trip is exact; check that NaN payloads survive, since
    assignment copies bits.
- **Resumable blocks.** Each command block becomes a Lua function `block(pc, run)`, a state machine dispatching on the
  command index. It is written as an if-chain rather than `goto`, so Luau stays possible. A wait yields and leaves `pc` on
  the waiting command.
  - For bodies are separate functions; a wait inside one returns from the body.
  - A rule set becomes a function that checks conditions in order; the first zero or NaN stops it.
- **Scheduler.** It keeps ScriptTick's order exactly:
  - waiting runs first, then instances, then dropping deleted objects, then timed effects, then freeing temporaries;
  - a new instance goes at the head of the instance list;
  - an instance started by a waiting run runs in the same tick, and one started by an instance's commands runs from the
    next tick;
  - Execute counts calls, which WaitSegment uses;
  - Sleep counts ticks as the original does.
- **Saves.**
  - New saves persist the whole Lua state with Eris: game-script coroutines and mod coroutines alike. The bindings form the
    "permanents" table; object references and groups persist as object ids through `__persist`, as the current save writes
    them.
  - Waits must yield from Lua, never from inside a C++ call.
  - Old saves (vanilla, or ours from before Lua) load through the resumable form: each saved run becomes
    `coroutine.create(function() return block(pc, run) end)` with its locals.
  - While no Lua-authored code is loaded, we can keep writing the old format as well, so saves stay vanilla-compatible.
    Whether to do that is decided in L6.
- **Mods.**
  - `.lua` files loaded next to the mission scripts.
  - The same built-ins under Lua names, with waits as yields.
  - A sandbox without io / os / debug.
  - Errors reported like "Script error: ...".

## Open decisions (at L2)

- **Lua 5.3 + Eris versus Luau + Ares** (the Luau port of Eris used by Second Life's SLua).
  - Eris supports 5.3 and has a 5.2 branch, but not 5.4: confirm this before vendoring.
  - Luau has no `goto`, but does have a sandbox and types; Ares is the less proven of the two.
- **Old-format saves.** Keep writing them while possible, or go Eris-only.
- **String encoding.** Lua strings carry cp1251 until U1-U5 switch to UTF-8; mods should see whatever the engine uses at
  that point.

## Verification

- **Lockstep (`luaboth`).** The old executor and Lua run side by side on the same world. Each tick compares the variables,
  run positions, instance lists, and a log of every built-in call with its arguments.
- **Coverage.** Every scenario, plus a corpus that loads each mission's scripts and runs N ticks.
- **Saves.** Round trips: old save loaded into Lua, Lua save loaded into Lua, and old save loaded into the old engine and
  compared.
- **Determinism.** The `deterministic` step and SV1's virtual clock make the runs repeatable.

## Risks

- **Timing.** Same-tick versus next-tick behaviour, and head insertion: lockstep catches it.
- **Floats.** Exactness through Lua numbers, and NaN handling.
- **Rebirth's quirks.** They have to be kept (`DistancePointPoint` reads four floats, `Random` with one argument, and the
  others in `rebirth.md`); the bindings call the same handlers, so they carry over.
- **Speed.** Rules are polled every tick, so each binding call has to stay cheap. Measure on the biggest mission.
