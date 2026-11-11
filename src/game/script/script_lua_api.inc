// The Lua API's declarations: every function, method, hook and field the game's Lua gives a script or reads from one,
// once. The runtime registers from these rows (script_lua.cpp: the keywords' Lua names, its own globals and the
// prelude's helpers; script_spell_host.cpp: the cast and stats methods); ei-convert --lua-api writes them as a LuaLS
// definition file and the reference tables of docs/modding.md (tools/convert/lua_api.cpp); ei-convert --lua-check
// checks the state against them (script_lua_api.h).
//
// EI_LUA_SECTION(title)                             the mission functions' rows that follow, in the docs under it
// EI_LUA_BINDING(keyword, name, params, returns, text)
//                                                   a keyword's binding under its Lua name (the naming rule:
//                                                   docs/notes/lua-scripting.md, "Bindings")
// EI_LUA_GLOBAL(name, params, returns, text)        the runtime's own global, the C function scriptlua::name
// EI_LUA_HELPER(name, params, returns, text)        the prelude's Lua function of that name, made a global
// EI_LUA_CAST_METHOD(name, params, returns, text)   cast:name, ScriptSpellHost::name
// EI_LUA_STATS_METHOD(name, params, returns, text)  stats:name, ScriptSpellHost::Stat<name>
// EI_LUA_EFFECT_METHOD(name, params, returns, text) effect:name in the one-shot hooks, ScriptSpellHost::Effect<name>
// EI_LUA_HOOK(name, params, returns, text)          a function a spell file's table may hold
// EI_LUA_FIELD(owner, name, type, text)             a field of a value the game gives (Cast, Effect, Flight, Point)
//                                                   or of an options table a method takes
// EI_LUA_STAT(name, text)                           a stats total (kEffectStatNames, in its order)
//
// params: "name: type" a comma apart; "name?: type" may be left out (the rest after it too); "...: type" any number
// more; a second form of the call after "; ". The types are LuaLS's with the game's short names: object (a map object,
// or nil for none), group, point ({x, y, z}), position (a point or an object, where it stands), cast, stats, effect,
// stat (a stats total's name) and the options' classes above. A condition's number holds unless it is 0.

#ifndef EI_LUA_SECTION
#define EI_LUA_SECTION(title)
#endif
#ifndef EI_LUA_BINDING
#define EI_LUA_BINDING(keyword, name, params, returns, text)
#endif
#ifndef EI_LUA_GLOBAL
#define EI_LUA_GLOBAL(name, params, returns, text)
#endif
#ifndef EI_LUA_HELPER
#define EI_LUA_HELPER(name, params, returns, text)
#endif
#ifndef EI_LUA_CAST_METHOD
#define EI_LUA_CAST_METHOD(name, params, returns, text)
#endif
#ifndef EI_LUA_STATS_METHOD
#define EI_LUA_STATS_METHOD(name, params, returns, text)
#endif
#ifndef EI_LUA_EFFECT_METHOD
#define EI_LUA_EFFECT_METHOD(name, params, returns, text)
#endif
#ifndef EI_LUA_HOOK
#define EI_LUA_HOOK(name, params, returns, text)
#endif
#ifndef EI_LUA_FIELD
#define EI_LUA_FIELD(owner, name, type, text)
#endif
#ifndef EI_LUA_STAT
#define EI_LUA_STAT(name, text)
#endif

// ---- Mission scripts: the global functions ---------------------------------------------------------------------

EI_LUA_SECTION("Script flow")
EI_LUA_HELPER(GlobalVars, "declarations: table<string, string>", "",
              "the script's globals, each set to its type's zero value: `{ name = \"float\" | \"string\" | \"object\" "
              "| \"group\" }` (0, `\"\"`, nil, an empty group)")
EI_LUA_HELPER(Script, "rules: {condition?: fun(...): number|boolean, action?: fun(...)}[]", "fun(...)",
              "a definition: each call starts an instance with those arguments, which checks its rules in order every "
              "tick and runs the action of one that holds")
EI_LUA_BINDING(Sleep, Sleep, "ticks: number|fun(): number", "",
               "waits that many ticks, then the function goes on (a function argument is called again on every tick)")
EI_LUA_BINDING(WaitSegment, WaitSegment, "ticks: number|fun(): number", "",
               "waits until the running instance has run that many ticks")
EI_LUA_BINDING(SleepUntil, SleepUntil, "condition: number|fun(): number", "",
               "waits until the condition holds (pass a function: it is called again on every tick)")
EI_LUA_BINDING(SleepUntilIdle, SleepUntilIdle, "unit: object|fun(): object", "",
               "waits while the unit's AI has an action under way")
EI_LUA_BINDING(KillScript, KillScript, "", "", "the running instance checks no more rules (not in the WorldScript)")
EI_LUA_BINDING(WaitEndAnimation, WaitEndAnimation, "", "", "does nothing (as in the game)")

EI_LUA_SECTION("Groups and loops")
EI_LUA_GLOBAL(NewGroup, "", "group", "an empty group")
EI_LUA_GLOBAL(CopyGroup, "group: group", "group",
              "a copy of the group (a group value is shared, as a table is; the game's assignment copied)")
EI_LUA_BINDING(AddObject, AddObject, "group: group, object: object", "", "puts the object into the group (once)")
EI_LUA_BINDING(RemoveObject, RemoveObject, "group: group, object: object", "", "takes the object out of the group")
EI_LUA_BINDING(GroupSize, GroupSize, "group: group", "number", "the number of objects in the group (also #group)")
EI_LUA_BINDING(GroupHas, GroupHas, "group: group, object: object", "number",
               "1 when the object is in the group, else 0")
EI_LUA_BINDING(GroupAdd, GroupUnion, "a: group, b: group", "group", "a new group of the objects in either")
EI_LUA_BINDING(GroupSub, GroupDifference, "a: group, b: group", "group", "a new group of a's objects that are not in b")
EI_LUA_BINDING(GroupCross, GroupIntersection, "a: group, b: group", "group", "a new group of the objects in both")
EI_LUA_BINDING(RandObj, RandomObject, "group: group", "object",
               "a member at random (the game's generator); nil for an empty group")
EI_LUA_BINDING(For, ForEach,
               "group: group, body: fun(member: object); counter: string, group: group, body: fun(member: object)", "",
               "calls the body for each member from the last to the first; a wait in the body ends it for that "
               "member. A counter global's name first: it holds each member in turn")
EI_LUA_BINDING(ForIf, ForEachIf,
               "group: group, condition: number|fun(member: object): number|boolean, body: fun(member: object); "
               "counter: string, group: group, condition: number|fun(member: object): number|boolean, body: "
               "fun(member: object)", "", "`ForEach`, the body only for the members the condition holds for")
EI_LUA_BINDING(Every, Every,
               "group: group, condition: number|fun(member: object): number|boolean; counter: string, group: group, "
               "condition: number|fun(member: object): number|boolean", "number",
               "1 when the condition holds for every member (from the first; it stops at one that fails), else 0")
EI_LUA_BINDING(Any, Any,
               "group: group, condition: number|fun(member: object): number|boolean; counter: string, group: group, "
               "condition: number|fun(member: object): number|boolean", "number",
               "1 when the condition holds for a member (from the first; it stops there), else 0")
EI_LUA_BINDING(Sum, Sum,
               "group: group, condition: number|fun(member: object): number|boolean; counter: string, group: group, "
               "condition: number|fun(member: object): number|boolean", "number",
               "the sum of the condition's values over the members")

EI_LUA_SECTION("Finding objects")
EI_LUA_BINDING(GetObject, GetObject, "id: number", "object", "the object with that id (nil when there is none)")
EI_LUA_BINDING(GetObjectByName, GetObjectByName, "name: string", "object",
               "the object whose id is the text key of that name")
EI_LUA_BINDING(GetObjectByID, GetObjectByID, "id: string", "object", "the object whose id is that decimal number")
EI_LUA_BINDING(GetLeader, GetLeader, "", "object",
               "the party leader: the server's in a network game, else player 0's first unit")
EI_LUA_BINDING(GetUnitOfPlayer, GetUnitOfPlayer, "player: number, index: number", "object",
               "the player's party member at that index, from 0")
EI_LUA_BINDING(NearestUnit, NearestUnit, "x: number, y: number, radius: number, filter: number", "object",
               "the nearest object within `radius` of (x, y) that the `filter` passes: + 1 units, + 2 other objects, "
               "+ 4 any living object with a named id")
EI_LUA_BINDING(UnitsNearPoint, UnitsNearPoint, "x: number, y: number, radius: number, filter: number", "group",
               "every object within `radius` of (x, y) that the filter passes (as `NearestUnit`'s)")
EI_LUA_BINDING(GetObjectsOnMap, GetObjectsOnMap, "class: number, ...: number", "group",
               "the world's objects whose class id is one of those given; -1: every object")
EI_LUA_BINDING(GetPlayerUnits, GetPlayerUnits, "player: number", "group", "the world's units of that player")
EI_LUA_BINDING(UnitSee, SeenByUnit, "unit: object", "group", "the objects the unit sees")
EI_LUA_BINDING(GroupSee, SeenByGroup, "group: group", "group", "the objects the group's units see")
EI_LUA_BINDING(PlayerSee, SeenByPlayer, "player: number", "group",
               "what the player sees: its units, what they see and what it has spotted")

EI_LUA_SECTION("Reading objects")
EI_LUA_BINDING(GetObjectID, GetObjectID, "object: object", "number", "the object's id; 0 for none")
EI_LUA_BINDING(GetObjectStringID, GetObjectStringID, "object: object", "string", "the object's id as a decimal string")
EI_LUA_BINDING(GetObjectTypeID, GetObjectTypeID, "object: object", "number", "the object's class id; -1 for none")
EI_LUA_BINDING(GetPrototype, GetPrototype, "unit: object", "string", "the unit's prototype name")
EI_LUA_BINDING(GetRace, GetRace, "unit: object", "string", "the unit's race")
EI_LUA_BINDING(GetX, GetX, "object: object", "number", "the object's x; 0 for none")
EI_LUA_BINDING(GetY, GetY, "object: object", "number", "the object's y; 0 for none")
EI_LUA_BINDING(GetZ, GetZ, "object: object", "number", "the object's height: the ground's at its place plus its own z")
EI_LUA_BINDING(GetFutureX, GetFutureX, "object: object, ticks: number", "number",
               "where the moving object's x will be that many ticks from now")
EI_LUA_BINDING(GetFutureY, GetFutureY, "object: object, ticks: number", "number",
               "where the moving object's y will be that many ticks from now")
EI_LUA_BINDING(DistanceUnitUnit, DistanceUnitUnit, "a: object, b: object", "number",
               "the distance between two objects on the ground plane (1e30 when one is missing)")
EI_LUA_BINDING(DistanceUnitPoint, DistanceUnitPoint, "object: object, x: number, y: number", "number",
               "the distance from the object to (x, y) on the ground plane (1e30 for none)")
EI_LUA_BINDING(DistancePointPoint, DistancePointPoint, "x: number, y: number", "number",
               "the distance of (x, y) from (0, 0): Rebirth's signature passes two of the four numbers its function "
               "reads")
EI_LUA_BINDING(IsInSquare, IsInSquare, "x: number, y: number, x1: number, y1: number, x2: number, y2: number", "number",
               "1 when (x, y) lies in the rectangle from (x1, y1) to (x2, y2), else 0")
EI_LUA_BINDING(UnitInSquare, UnitInSquare, "object: object, x1: number, y1: number, x2: number, y2: number", "number",
               "1 when the object stands in the rectangle from (x1, y1) to (x2, y2), else 0")
EI_LUA_BINDING(IsUnitInWater, IsUnitInWater, "object: object", "number", "1 when the object stands in water, else 0")
EI_LUA_BINDING(IsUnitVisible, IsUnitVisible, "object: object", "number", "1 when player 0 sees the object, else 0")
EI_LUA_BINDING(IsUnitBlocked, IsUnitBlocked, "object: object", "number",
               "1 when the object is blocked (`BlockUnit`), else 0")
EI_LUA_BINDING(WasLooted, WasLooted, "object: object", "number",
               "1 when the object's loot was taken (or it is gone), else 0")
EI_LUA_BINDING(GetAIClass, GetAIClass, "unit: object", "number", "the unit's size class; -1 for none")
EI_LUA_BINDING(IsPlayer, IsPlayer, "unit: object", "number", "1 when the unit is a named party member, else 0")

EI_LUA_SECTION("Units' stats")
EI_LUA_BINDING(IsAlive, IsAlive, "object: object", "number", "1 when the object is not dead, else 0 (also for none)")
EI_LUA_BINDING(IsDead, IsDead, "object: object", "number", "the object's death state: 0 alive, not 0 dead (0 for none)")
EI_LUA_BINDING(HP, HitPoints, "unit: object", "number", "the unit's hit points")
EI_LUA_BINDING(MaxHP, MaxHitPoints, "unit: object", "number", "the unit's maximum hit points")
EI_LUA_BINDING(Mana, Mana, "unit: object", "number", "the unit's mana")
EI_LUA_BINDING(MaxMana, MaxMana, "unit: object", "number", "the unit's maximum mana")
EI_LUA_BINDING(GetStrength, GetStrength, "unit: object", "number", "the unit's strength")
EI_LUA_BINDING(GetDexterity, GetDexterity, "unit: object", "number", "the unit's dexterity")
EI_LUA_BINDING(GetIntelligence, GetIntelligence, "unit: object", "number", "the unit's intelligence")
EI_LUA_BINDING(GetSkill, GetSkill, "unit: object, skill: string", "number",
               "the unit's skill: science, melee, archery, elemental, sense or astral")
EI_LUA_BINDING(GetPerk, GetPerk, "unit: object, perk: string", "number", "the unit's level of the perk by its name")
EI_LUA_BINDING(GetActions, GetActions, "unit: object", "number", "the unit's action rate")
EI_LUA_BINDING(GetActionsBase, GetActionsBase, "unit: object", "number", "the unit's action rate before its effects")
EI_LUA_BINDING(GetEncumbrance, GetEncumbrance, "unit: object", "number", "the weight the unit carries")
EI_LUA_BINDING(GetMaxEncumbrance, GetMaxEncumbrance, "unit: object", "number", "the weight the unit can carry")
EI_LUA_BINDING(GetArmor, GetArmor, "unit: object, kind: number, part: number", "number",
               "the unit's protection of the body part (0 to 6) by damage kind (0 to 5); a negative kind: the part's "
               "total")
EI_LUA_BINDING(GetUnitExp, GetUnitExperience, "unit: object, what?: string", "number",
               "the unit's experience: \"max\" (the default), \"spent\", \"penalty\" or \"real\" (max + penalty)")

EI_LUA_SECTION("Changing objects")
EI_LUA_BINDING(SetCP, SetPosition, "object: object, x: number, y: number, z: number", "",
               "moves the object there; units near a moved object step off cells they cannot stay in")
EI_LUA_BINDING(SetCPFast, SetPositionFast, "object: object, x: number, y: number, z: number", "",
               "moves the object there without moving the units around it")
EI_LUA_BINDING(HideObject, HideObject, "object: object, hide: number", "", "hides the object (not 0) or shows it (0)")
EI_LUA_BINDING(BlockUnit, BlockUnit, "unit: object, block: number", "",
               "the unit is blocked (not 0: it does not act) or free (0)")
EI_LUA_BINDING(KillUnit, KillUnit, "object: object", "", "kills the object (a plain hit that kills)")
EI_LUA_BINDING(InflictDamage, InflictDamage, "unit: object, amount: number", "",
               "the unit takes a hit of that much lightning damage")
EI_LUA_BINDING(SetPlayer, SetPlayer, "unit: object, player: number", "",
               "the unit joins the player's side, with its enemies")
EI_LUA_BINDING(SetEnemy, SetEnemy, "unit: object, player: number", "", "the unit takes the player's side as an enemy")
EI_LUA_BINDING(GiveStrength, GiveStrength, "unit: object, amount: number", "", "raises the unit's strength")
EI_LUA_BINDING(GiveDexterity, GiveDexterity, "unit: object, amount: number", "", "raises the unit's dexterity")
EI_LUA_BINDING(GiveIntelligence, GiveIntelligence, "unit: object, amount: number", "", "raises the unit's intelligence")
EI_LUA_BINDING(GiveSkill, GiveSkill, "unit: object, skill: string, amount: number", "",
               "raises the unit's skill: science, melee, archery, backstab, elemental, sense, astral, stealth or "
               "awareness")
EI_LUA_BINDING(GiveUnitExp, GiveUnitExperience, "unit: object, amount: number", "",
               "the unit gains that much experience")
EI_LUA_BINDING(GiveUnitSpell, GiveUnitSpell, "unit: object, spell: string", "",
               "the unit gets the spell made from that text")
EI_LUA_BINDING(GiveUnitQuestItem, GiveUnitQuestItem, "unit: object, item: string", "",
               "the unit gets the quest item of that name")
EI_LUA_BINDING(RemoveQuestItem, RemoveQuestItem, "unit: object, id: number", "",
               "takes the unit's first quest item of that id")
EI_LUA_BINDING(GiveUnitWeapon, GiveUnitWeapon, "unit: object, item: string, override: number", "",
               "the unit gets the weapon of that name; `override` 1 (a unit without a named id) also takes its stats "
               "from it")
EI_LUA_BINDING(GiveUnitArmor, GiveUnitArmor, "unit: object, item: string, override: number", "",
               "the unit gets the armour of that name; `override` 1 (a unit without a named id) also takes its "
               "protection from it")
EI_LUA_BINDING(AddUnitToServer, AddUnitToServer,
               "id: string, prototype: string, name: string, x: number, y: number, z: number", "",
               "a new unit of the prototype at (x, y, z), its id the text key of `id`")
EI_LUA_BINDING(RemoveUnitFromServer, RemoveUnitFromServer, "unit: object", "", "takes the unit out of the world")
EI_LUA_BINDING(RemoveObjectFromServer, RemoveObjectFromServer, "object: object", "",
               "takes the object out of the world")
EI_LUA_BINDING(PlayAnimation, PlayAnimation, "unit: object, animation: string", "", "the unit plays the animation once")
EI_LUA_BINDING(StartAnimation, StartAnimation, "unit: object, animation: string", "", "does nothing (as in the game)")
EI_LUA_BINDING(SetSpellAggression, SetSpellAggression, "unit: object, value: number", "",
               "does nothing (as in the game)")
EI_LUA_BINDING(SetDirectionToObject, SetDirectionToObject, "value: number", "", "does nothing (as in the game)")

EI_LUA_SECTION("Unit orders")
EI_LUA_BINDING(MoveToPoint, MoveToPoint, "unit: object, x: number, y: number", "", "the unit walks to (x, y)")
EI_LUA_BINDING(MoveToObject, MoveToObject, "unit: object, target: object", "",
               "the unit walks to where the target stands")
EI_LUA_BINDING(RotateTo, RotateTo, "unit: object, x: number, y: number", "", "the unit turns towards (x, y)")
EI_LUA_BINDING(Follow, Follow, "unit: object, leader: object", "", "the unit follows the leader")
EI_LUA_BINDING(Attack, Attack, "unit: object, target: object", "", "the unit attacks the target")
EI_LUA_BINDING(ResetTarget, ResetTarget, "unit: object", "", "the unit drops its target and goes back to its behaviour")
EI_LUA_BINDING(Cast, Cast, "unit: object, x: number, y: number", "",
               "the unit casts its first spell at (x, y) on the ground")
EI_LUA_BINDING(CastSpellPoint, CastSpellPoint, "spell: string, x: number, y: number, toX: number, toY: number", "",
               "the spell made from that text cast with no caster from (x, y) at the point (toX, toY)")
EI_LUA_BINDING(CastSpellUnit, CastSpellUnit, "spell: string, x: number, y: number, target: object", "",
               "the spell made from that text cast with no caster from (x, y) at the target")
EI_LUA_BINDING(Idle, Idle, "unit: object", "", "the unit's modes cleared and its action stopped")
EI_LUA_BINDING(Guard, Guard, "unit: object, x: number, y: number, radius: number", "",
               "the unit guards the circle around (x, y)")
EI_LUA_BINDING(Sentry, Sentry, "unit: object, x: number, y: number", "", "the unit stands sentry at (x, y)")
EI_LUA_BINDING(Lie, Lie, "unit: object", "", "the unit lies down")
EI_LUA_BINDING(Crawl, Crawl, "unit: object", "", "the unit crawls")
EI_LUA_BINDING(Stand, Stand, "unit: object", "", "the unit walks upright")
EI_LUA_BINDING(Walk, Walk, "unit: object", "", "the unit walks")
EI_LUA_BINDING(Run, Run, "unit: object", "", "the unit runs")
EI_LUA_BINDING(Rest, Rest, "unit: object", "", "the unit stops what it has queued")

EI_LUA_SECTION("Unit modes (the AI)")
EI_LUA_BINDING(UMClear, UnitModeClear, "unit: object", "", "takes every mode off the unit")
EI_LUA_BINDING(UMStandard, UnitModeStandard, "unit: object", "", "adds aggression, suspicion and the corpse watcher")
EI_LUA_BINDING(UMAg, UnitModeAggressionLevel, "unit: object, level: number", "",
               "the unit's temper from 0 (fearful) to 3 (aggressive): its aggression, fear, suspicion and corpse "
               "watcher modes replaced")
EI_LUA_BINDING(UMAggression, UnitModeAggression, "unit: object", "",
               "adds aggression: the unit attacks enemies it notices")
EI_LUA_BINDING(UMRevenge, UnitModeRevenge, "unit: object", "", "adds revenge: aggression against who attacks it")
EI_LUA_BINDING(UMFear, UnitModeFear, "unit: object, fearful: number", "",
               "adds fear: the unit flees (not 0: from any enemy)")
EI_LUA_BINDING(UMSuspection, UnitModeSuspicion, "unit: object", "",
               "adds suspicion: the unit goes to look at what it hears")
EI_LUA_BINDING(UMCorpseWatcher, UnitModeCorpseWatcher, "unit: object", "",
               "adds the corpse watcher: the unit raises the alarm at corpses")
EI_LUA_BINDING(UMSentry, UnitModeSentry, "unit: object, x: number, y: number", "", "adds a sentry post at (x, y)")
EI_LUA_BINDING(UMGuard, UnitModeGuard, "unit: object, x: number, y: number, radius: number", "",
               "adds guarding the circle around (x, y)")
EI_LUA_BINDING(UMGuardEx, UnitModeGuardEx, "unit: object, x: number, y: number, radius: number, chase: number", "",
               "adds guarding the circle around (x, y), chasing up to the second radius")
EI_LUA_BINDING(UMFollow, UnitModeFollow, "unit: object, leader: object", "", "adds following the leader")
EI_LUA_BINDING(UMPatrol, UnitModePatrol, "unit: object", "", "adds a patrol (its points from `UnitModePatrolAddPoint`)")
EI_LUA_BINDING(UMPatrolClear, UnitModePatrolClear, "unit: object", "", "empties the unit's patrol route")
EI_LUA_BINDING(UMPatrolAddPoint, UnitModePatrolAddPoint, "unit: object, x: number, y: number", "",
               "adds a point to the unit's patrol route")
EI_LUA_BINDING(UMPatrolAddPointLook, UnitModePatrolAddPointLook,
               "unit: object, angle: number, wait: number, turn: number", "",
               "at the route's last point the unit looks towards the angle for `wait`, turning in `turn`")
EI_LUA_BINDING(UMPlayer, UnitModePlayer, "unit: object", "",
               "the unit's modes replaced by the player's (it obeys its player)")

EI_LUA_SECTION("Players, parties and diplomacy")
EI_LUA_BINDING(GetPlayer, GetPlayer, "unit: object", "number", "the unit's side (player number)")
EI_LUA_BINDING(GetPlayerOfUnit, GetPlayerOfUnit, "unit: object", "number",
               "the player whose party the unit is in; -1 for none")
EI_LUA_BINDING(PlayerExists, PlayerExists, "player: number", "number",
               "1 when there is a player of that number, else 0")
EI_LUA_BINDING(AddUnitUnderControl, AddUnitUnderControl, "player: number, unit: object", "",
               "the player controls the unit")
EI_LUA_BINDING(RemoveUnitFromControl, RemoveUnitFromControl, "player: number, unit: object", "",
               "no player controls the unit")
EI_LUA_BINDING(GetDiplomacy, GetDiplomacy, "a: number, b: number", "number",
               "the two players' relation: 1 allied, -1 hostile, 0 neutral")
EI_LUA_BINDING(SetDiplomacy, SetDiplomacy, "a: number, b: number, relation: number", "",
               "sets the two players' relation: above 0 allied, below 0 hostile, else neutral")
EI_LUA_BINDING(IsEnemy, IsEnemy, "unit: object, player: number", "number",
               "1 when the unit is hostile to the player, else 0")
EI_LUA_BINDING(SetPlayerAggression, SetPlayerAggression, "player: number, value: number", "",
               "does nothing (as in the game)")
EI_LUA_BINDING(IsPlayerInDanger, IsPlayerInDanger, "player: number", "number",
               "1 when the player's party is in danger, else 0")
EI_LUA_BINDING(IsPlayerInSafety, IsPlayerInSafety, "player: number", "number",
               "1 when the player's party is safe, else 0")
EI_LUA_BINDING(GodMode, GodMode, "player: number, on: number", "", "the player's units cannot die (not 0) or can (0)")
EI_LUA_BINDING(GetMercsNumber, GetMercenaryCount, "player: number", "number", "the player's living mercenaries")
EI_LUA_BINDING(RecalcMercBriefings, RecalculateMercenaryBriefings, "", "",
               "the mercenaries' briefings worked out again")
EI_LUA_BINDING(CreateParty, CreateParty, "player: number, party: string", "", "a new party record of that name")
EI_LUA_BINDING(RemoveParty, RemoveParty, "player: number, party: string", "", "removes the party record")
EI_LUA_BINDING(SetCurrentParty, SetCurrentParty, "player: number, party: string", "", "the party the player plays next")
EI_LUA_BINDING(SendCurrentParty, SendCurrentParty, "player: number", "", "sends the player's party to its client again")
EI_LUA_BINDING(AddUnitToParty, AddUnitToParty, "player: number, unit: string, prototype: string", "",
               "adds a unit of the prototype to a party record (\"party.unit\")")
EI_LUA_BINDING(RemoveUnitFromParty, RemoveUnitFromParty, "player: number, unit: string", "",
               "removes the unit (\"party.unit\") from its party record")
EI_LUA_BINDING(RedeployParty, RedeployParty, "player: number", "", "places the player's party again at its start")
EI_LUA_BINDING(CopyItems, CopyItems, "player: number, from: string, to: string", "",
               "one party unit's items copied to another (\"party.unit\")")
EI_LUA_BINDING(CopyStats, CopyStats, "player: number, from: string, to: string", "",
               "one party unit's stats copied to another (\"party.unit\")")
EI_LUA_BINDING(LeaveToZone, LeaveToZone, "player: number, zone: string, entrance: number", "",
               "the player's party leaves for the zone, at that entrance")
EI_LUA_BINDING(ShowAllUnits, ShowAllUnits, "show: number", "",
               "0 draws every unit; anything else only the visible ones")

EI_LUA_SECTION("Items and money")
EI_LUA_BINDING(GiveItem, GiveItem, "player: number, item: string", "", "the player gets the item of that name")
EI_LUA_BINDING(GiveQuestItem, GiveQuestItem, "player: number, item: string", "",
               "the player gets the quest item of that name")
EI_LUA_BINDING(EraseQuestItem, EraseQuestItem, "player: number, id: number", "",
               "takes the player's quest item of that id")
EI_LUA_BINDING(HaveItem, HaveItem, "player: number, id: number", "number",
               "1 when the player has the quest item of that id, else 0")
EI_LUA_BINDING(GetLootItemsCount, GetLootItemsCount, "player: number", "number", "the number of the player's items")
EI_LUA_BINDING(GetMoney, GetMoney, "player: number", "number", "the player's party's money")
EI_LUA_BINDING(GiveMoney, GiveMoney, "player: number, amount: number", "", "the player's party gets that much money")
EI_LUA_BINDING(AddLoot, AddLoot, "player: number, from: string, to: string", "",
               "one party record's items and money added to another's")
EI_LUA_BINDING(CopyLoot, CopyLoot, "player: number, from: string, to: string", "",
               "one party record's items and money replace another's")
EI_LUA_BINDING(Loot, Loot, "player: number, unit: object", "", "the player loots the dead unit, which then goes")
EI_LUA_BINDING(FixItems, FixItems, "", "", "repairs every item")
EI_LUA_BINDING(GiveExp, GiveExperience, "player: number, amount: number", "",
               "the player's party is awarded that much experience")

EI_LUA_SECTION("Quests and game variables")
EI_LUA_BINDING(QStart, QuestStart, "name: string", "", "starts building the quest of that name")
EI_LUA_BINDING(QObjArea, QuestObjectiveArea, "area: number", "", "the quest's objective: the heroes reach the area")
EI_LUA_BINDING(QObjSeeObject, QuestObjectiveSeeObject, "object: string", "",
               "the quest's objective: the heroes come near the object (a script name)")
EI_LUA_BINDING(QObjSeeUnit, QuestObjectiveSeeUnit, "unit: string", "", "the quest's objective: the heroes see the unit")
EI_LUA_BINDING(QObjKillUnit, QuestObjectiveKillUnit, "unit: string", "", "the quest's objective: the unit dies")
EI_LUA_BINDING(QObjKillGroup, QuestObjectiveKillGroup, "group: string", "",
               "the quest's objective: every unit of the group dies")
EI_LUA_BINDING(QObjUse, QuestObjectiveUse, "lever: string, state: number", "",
               "the quest's objective: the lever is in that state")
EI_LUA_BINDING(QObjGetItem, QuestObjectiveGetItem, "id: number", "",
               "the quest's objective: the heroes have the quest item")
EI_LUA_BINDING(QFinish, QuestFinish, "", "", "ends the quest begun by `QuestStart` and adds its script")
EI_LUA_BINDING(QuestComplete, QuestComplete, "player: number, quest: string", "",
               "the quest is done: its experience to the player")
EI_LUA_BINDING(GSSetVar, SetGameVar, "player: number, name: string, value: number", "",
               "sets the game variable (the game server's in a network game)")
EI_LUA_BINDING(GSSetVarMax, SetGameVarMax, "player: number, name: string, value: number", "",
               "raises the game variable to the value when it is lower")
EI_LUA_BINDING(GSDelVar, DeleteGameVar, "player: number, name: string", "", "deletes the game variable")
EI_LUA_BINDING(GSGetVar, GetGameVar, "player: number, name: string", "number",
               "the game variable's value (0 when not set)")
EI_LUA_BINDING(GSSetPlayerVar, SetPlayerGameVar, "player: number, name: string, value: number", "",
               "sets the player's own variable")
EI_LUA_BINDING(GSGetPlayerVar, GetPlayerGameVar, "player: number, name: string", "number", "the player's own variable")
EI_LUA_BINDING(GSListPlayerVars, ListPlayerGameVars, "player: number", "",
               "writes the player's variables to the console")

EI_LUA_SECTION("Areas and alarms")
EI_LUA_BINDING(AddRectToArea, AddRectToArea, "area: number, x1: number, y1: number, x2: number, y2: number", "",
               "adds the rectangle from (x1, y1) to (x2, y2) to the area of that number")
EI_LUA_BINDING(AddRoundToArea, AddRoundToArea, "area: number, x: number, y: number, radius: number", "",
               "adds the circle around (x, y) to the area of that number")
EI_LUA_BINDING(DeleteArea, DeleteArea, "area: number", "", "deletes the area")
EI_LUA_BINDING(IsInArea, IsInArea, "area: number, x: number, y: number", "number",
               "1 when (x, y) lies in the area, else 0")
EI_LUA_BINDING(InvokeAlarm, InvokeAlarm, "alarm: number, x: number, y: number", "",
               "raises the alarm of that number at (x, y)")
EI_LUA_BINDING(IsAlarm, IsAlarm, "alarm: number", "number", "1 while the alarm is raised, else 0")
EI_LUA_BINDING(AlarmTime, AlarmTime, "alarm: number", "number", "the tick the alarm was raised at")
EI_LUA_BINDING(AlarmPosX, AlarmPositionX, "alarm: number", "number", "the x where the alarm was raised")
EI_LUA_BINDING(AlarmPosY, AlarmPositionY, "alarm: number", "number", "the y where the alarm was raised")

EI_LUA_SECTION("Levers and traps")
EI_LUA_BINDING(GetLeverState, GetLeverState, "lever: object", "number", "the lever's state")
EI_LUA_BINDING(SwitchLeverState, SwitchLeverState, "lever: object, state: number", "",
               "switches the lever to the state")
EI_LUA_BINDING(SwitchLeverStateEx, SwitchLeverStateEx, "lever: object, state: number, time: number", "",
               "switches the lever to the state over that time")
EI_LUA_BINDING(EnableLever, EnableLever, "lever: object, on: number", "", "the lever can be used (not 0) or not (0)")
EI_LUA_BINDING(SetScience, SetScience,
               "lever: object, key: number, level: number, a: number, b: number, c: number, d: number", "",
               "locks the lever: the key's id, the lock's level and four flags (not 0: set)")
EI_LUA_BINDING(ActivateTrap, ActivateTrap, "trap: object, on: number", "", "arms the trap (not 0) or disarms it (0)")

EI_LUA_SECTION("World, time and weather")
EI_LUA_BINDING(GetWorldTime, GetWorldTime, "", "number", "the hour of the day, 0 to 24")
EI_LUA_BINDING(IsNight, IsNight, "", "number", "how much night it is, 0 by day")
EI_LUA_BINDING(RunWorldTime, RunWorldTime, "hour: number", "", "sets the hour and lets the time run")
EI_LUA_BINDING(FixWorldTime, FixWorldTime, "hour: number", "", "sets the hour and stops the time there")
EI_LUA_BINDING(SetWeather, SetWeather, "weather: string", "", "\"rain\", \"snow\" or \"clear\"")
EI_LUA_BINDING(SetWind, SetWind, "x: number, y: number, z: number, strength: number", "",
               "the wind's direction and strength")
EI_LUA_BINDING(SetWaterLevel, SetWaterLevel, "material: number, level: number, ticks: number", "",
               "the water of the terrain material rises or falls to the level over that many ticks")
EI_LUA_BINDING(SetBackGroundColor, SetBackgroundColor, "r: number, g: number, b: number", "",
               "the fog's colour, 0 to 255 each")
EI_LUA_BINDING(SetSunLight, SetSunLight,
               "a: number, b: number, c: number, d: number, e: number, f: number, g: number, h: number, i: number", "",
               "does nothing (as in the game)")
EI_LUA_BINDING(GetZValue, GetGridHeight, "x: number, y: number", "number",
               "the ground's height at (x, y) on the terrain grid")
EI_LUA_BINDING(GetBSZValue, GetSurfaceHeight, "x: number, y: number", "number",
               "the surface's height at (x, y), objects such as bridges included")
EI_LUA_BINDING(AddMob, AddMob, "file: string", "", "loads the objects of the .mob file into the zone")
EI_LUA_BINDING(AddMobEx, AddMobEx, "file: string, keepIds?: number", "",
               "`AddMob` without the loading movie; `keepIds` 1: the objects keep their ids")
EI_LUA_BINDING(PauseGame, PauseGame, "pause: number", "",
               "outside network games: 0 pauses the game, anything else goes on")
EI_LUA_BINDING(GameOver, GameOver, "", "", "the game is over for player 0")
EI_LUA_BINDING(GameOverMSG, GameOverMessage, "title: string, text: string", "",
               "the game is over for player 0, with a message")
EI_LUA_BINDING(IsFileExists, FileExists, "path: string", "number", "1 when the mod's folder holds the file, else 0")

EI_LUA_SECTION("Lights, particles and sounds")
EI_LUA_BINDING(CreatePointLight, CreatePointLight,
               "id: number, x: number, y: number, z: number, radius: number, r: number, g: number, b: number", "",
               "a point light (a light of that id replaced), its colour 0 to 255 each")
EI_LUA_BINDING(MovePointLight, MovePointLight,
               "id: number, x: number, y: number, z: number, ticks: number, remove: number", "",
               "the light moves to (x, y, z) over that many ticks; `remove` 1: then it goes")
EI_LUA_BINDING(DeletePointLight, DeletePointLight, "id: number", "", "removes the light")
EI_LUA_BINDING(CreateParticleSource, CreateParticleSource,
               "id: number, x: number, y: number, z: number, size: number, effect: string", "",
               "a particle effect (the game's by its name or one of `Res\\effects`); a negative id takes a new one")
EI_LUA_BINDING(MoveParticleSource, MoveParticleSource,
               "id: number, x: number, y: number, z: number, ticks: number, remove: number", "",
               "the effect moves to (x, y, z) over that many ticks; `remove` 1: then it goes")
EI_LUA_BINDING(DeleteParticleSource, DeleteParticleSource, "id: number", "", "removes the particle effect")
EI_LUA_BINDING(AttachParticleSource, AttachParticleSource, "id: number, object: number", "",
               "the effect follows the object of that id")
EI_LUA_BINDING(AttachParticles, AttachParticles, "id: number, object: object", "", "the effect follows the object")
EI_LUA_BINDING(SetParticleSourceSize, SetParticleSourceSize, "id: number, size: number", "", "the effect's size")
EI_LUA_BINDING(SetParticleInput, SetParticleInput, "id: number, input: string, value: number, y?: number, z?: number",
               "", "an input of one of your effects (`Res\\effects`) set for every player: a number, a flag (0 / 1) or "
               "a choice's index, or a vector by three numbers")
EI_LUA_BINDING(FireParticleEvent, FireParticleEvent, "id: number, event: string", "",
               "an event input of one of your effects fired for every player")
EI_LUA_BINDING(CreateLightning, CreateLightning,
               "id: number, x1: number, y1: number, z1: number, x2: number, y2: number, z2: number, width: number", "",
               "a lightning bolt from one point to the other; a negative id takes a new one")
EI_LUA_BINDING(DeleteLightning, DeleteLightning, "id: number", "", "removes every bolt of that id")
EI_LUA_BINDING(CreateFX, CreateSound, "x: number, y: number, z: number, volume: number, radius: number, sound: string",
               "", "plays the sound once at (x, y, z)")
EI_LUA_BINDING(CreateFXSource, CreateSoundSource,
               "id: number, x: number, y: number, z: number, volume: number, radius: number, sound: string", "",
               "a looping sound at (x, y, z); a negative id takes a new one")
EI_LUA_BINDING(CreateRandomizedFXSource, CreateRandomizedSoundSource,
               "id: number, x: number, y: number, z: number, volume: number, radius: number, minDelay: number, "
               "maxDelay: number, spread: number, sound: string", "",
               "a sound played again and again after random delays (ticks), spread around (x, y, z)")
EI_LUA_BINDING(DeleteFXSource, DeleteSoundSource, "id: number", "", "removes the sound source")
EI_LUA_BINDING(PlayFX, PlaySound, "x: number, y: number, z: number, volume: number, sound: string", "",
               "does nothing (as in the game)")

EI_LUA_SECTION("Camera, movies, music and messages")
EI_LUA_BINDING(PlayCamera, PlayCamera, "path: string", "", "plays the camera path")
EI_LUA_BINDING(IsCameraPlaying, IsCameraPlaying, "", "number", "not 0 while a camera path plays")
EI_LUA_BINDING(StartCameraSave, StartCameraSave, "file: string", "", "records the camera's moves into the file")
EI_LUA_BINDING(StopCameraSave, StopCameraSave, "", "", "stops recording the camera")
EI_LUA_BINDING(SetCameraOrientation, SetCameraOrientation, "angle: number", "", "does nothing (as in the game)")
EI_LUA_BINDING(SetCameraPosition, SetCameraPosition, "x: number, y: number, z: number", "",
               "does nothing (as in the game)")
EI_LUA_BINDING(PlayMovie, PlayMovie, "file: string", "", "plays the movie of the `Movies` folder")
EI_LUA_BINDING(PlayMusic, PlayMusic, "player: number, music: string", "", "the player hears the jingle")
EI_LUA_BINDING(ShowCredits, ShowCredits, "file: string", "", "does nothing (as in the game)")
EI_LUA_BINDING(ShowBitmap, ShowBitmap, "value: number, file: string", "", "does nothing (as in the game)")
EI_LUA_BINDING(SendEvent, SendEvent, "kind: number, value: number", "", "sends the value to every player")
EI_LUA_BINDING(SendStringEvent, SendStringEvent, "kind: number, text: string", "", "sends the text to every player")
EI_LUA_BINDING(Say, Say, "text: string, ...: string", "",
               "in a network game, the texts joined as a chat line from the server")
EI_LUA_BINDING(ConsoleFloat, ConsoleFloat, "value: number", "", "writes the number as a console line")
EI_LUA_BINDING(ConsoleString, ConsoleString, "text: string, ...: string", "",
               "writes the texts joined as a console line")

EI_LUA_SECTION("Numbers and strings")
EI_LUA_BINDING(IsEqual, IsEqual, "a: number, b: number", "number", "1 when a equals b, else 0")
EI_LUA_BINDING(IsLess, IsLess, "a: number, b: number", "number", "1 when a is less than b, else 0")
EI_LUA_BINDING(IsGreater, IsGreater, "a: number, b: number", "number", "1 when a is greater than b, else 0")
EI_LUA_BINDING(IsEqualString, IsEqualString, "a: string, b: string", "number", "1 when the strings are equal, else 0")
EI_LUA_BINDING(Not, Not, "a: number", "number", "1 when a is 0, else 0")
EI_LUA_BINDING(And, And, "a: number, b: number, ...: number", "number", "1 when no argument truncates to 0, else 0")
EI_LUA_BINDING(Or, Or, "a: number, b: number, ...: number", "number",
               "1 when an argument does not truncate to 0, else 0")
EI_LUA_BINDING(Add, Add, "a: number, b: number, ...: number", "number", "the sum, in single precision")
EI_LUA_BINDING(Sub, Sub, "a: number, b: number", "number", "a - b, in single precision")
EI_LUA_BINDING(Mul, Mul, "a: number, b: number, ...: number", "number", "the product, in single precision")
EI_LUA_BINDING(Div, Div, "a: number, b: number", "number", "a / b, in single precision")
EI_LUA_BINDING(Max, Max, "a: number, b: number, ...: number", "number", "the largest")
EI_LUA_BINDING(Min, Min, "a: number, b: number, ...: number", "number", "the smallest")
EI_LUA_BINDING(Abs, Abs, "a: number", "number", "a without its sign")
EI_LUA_BINDING(Round, Round, "a: number", "number", "a rounded to the nearest whole number, halves away from 0")
EI_LUA_BINDING(Ceil, Ceil, "a: number", "number", "the smallest whole number not below a")
EI_LUA_BINDING(Floor, Floor, "a: number", "number", "the largest whole number not above a")
EI_LUA_BINDING(Sin, Sin, "radians: number", "number", "the sine")
EI_LUA_BINDING(Cos, Cos, "radians: number", "number", "the cosine")
EI_LUA_BINDING(Pow, Pow, "a: number, b: number", "number", "a to the power b")
EI_LUA_BINDING(Random, Random, "a: number, b?: number", "number",
               "a draw of the game's generator: from 0 to `a`; with `b`, from `a` to `b`")
EI_LUA_BINDING(RandStr, RandomString, "a: string, b: string, ...: string", "string",
               "one of the strings at random (the game's generator)")
EI_LUA_BINDING(FloatToStr, FloatToString, "value: number, digits?: number", "string",
               "the number in decimals (3 unless given, 0 to 8), trailing zeros cut")
EI_LUA_BINDING(StrToFloat, StringToFloat, "text: string", "number", "the number the text starts with (0 when none)")
EI_LUA_BINDING(StrCat, StringConcat, "a: string, b: string, ...: string", "string", "the strings joined")
EI_LUA_BINDING(StrLen, StringLength, "text: string", "number", "the text's length in characters")
EI_LUA_BINDING(StrHas, StringContains, "text: string, part: string", "number",
               "1 when the text contains the part, else 0")
EI_LUA_BINDING(StrMid, StringMid, "text: string, start: number, count: number", "string",
               "`count` characters from `start` (1 the first, -1 the last; `count` 0: the rest)")
EI_LUA_BINDING(StrLower, StringLower, "text: string", "string", "the text in lower case")
EI_LUA_BINDING(StrUpper, StringUpper, "text: string", "string", "the text in upper case")

// ---- Spells: the hooks a spell file's table may hold -----------------------------------------------------------

EI_LUA_HOOK(OnCast, "cast: cast", "", "once, when the cast starts")
EI_LUA_HOOK(OnTick, "cast: cast", "",
            "every tick after `OnCast` (a file with it keeps its casts going until `cast:Finish()` or the limit)")
EI_LUA_HOOK(OnHit, "cast: cast, target: object, payload: any, point: point", "",
            "a missile of the cast landed: `target` nil when the unit is gone, `payload` what `cast:Missile` was "
            "given, `point` where it ended")
EI_LUA_HOOK(OnEnd, "cast: cast, reason: \"finished\"|\"idle\"|\"limit\"|\"error\"", "",
            "once, at the end of the cast: `reason` `\"finished\"`, `\"idle\"`, `\"limit\"` or `\"error\"`")
EI_LUA_HOOK(OnResume,
            "cast: cast, old: {state: \"missile\"|\"lasting\", ticks: integer, visual?: integer, sound?: integer, "
            "light?: integer, direction?: point}", "",
            "a save made before a game's spell moved onto this file held a cast of it in flight (`old.state` "
            "`\"missile\"`) or lingering (`\"lasting\"`), `old.ticks` left on the game's own count: the cast's "
            "objects are already its own (a missile still lands in `OnHit`, with a nil payload; `old.visual`, "
            "`old.sound` and `old.light` are their handles, for `cast:Release` later; `old.direction` the direction "
            "of its wall or bolt); set its timers and data here (the world methods are off in this hook, but "
            "`cast:Block`). Without it such a cast ends at once")
EI_LUA_HOOK(OnStats, "effect: effect, stats: stats", "",
            "the effect's kind on a unit works out its stats ([Active effects](#active-effects)); the world's "
            "functions are off in it")
EI_LUA_HOOK(OnEffectApplied, "effect: ActiveEffect, unit: object", "",
            "an effect of the spell's kind came on a unit ([Active effects](#active-effects)), run at the end of "
            "that tick")
EI_LUA_HOOK(OnEffectEnded,
            "effect: ActiveEffect, unit: object, reason: \"expired\"|\"removed\"|\"renewed\"|\"died\"", "",
            "an effect of the spell's kind left a unit: `reason` `\"expired\"` (its ticks ran out), `\"removed\"` "
            "(`cast:Remove`, `effect:Remove`), `\"renewed\"` (a new one of the kind and strength replaced it) or "
            "`\"died\"` (the unit died)")
EI_LUA_HOOK(OnEffectTick, "effect: ActiveEffect, unit: object", "",
            "every tick an effect of the spell's kind lasts on a unit, after its count down (`effect.ticks` left, "
            "above 0)")

// ---- Spells: the cast value ------------------------------------------------------------------------------------

EI_LUA_FIELD(Cast, id, "integer", "the cast's number")
EI_LUA_FIELD(Cast, code, "string", "the spell's code")
EI_LUA_FIELD(Cast, caster, "object", "the unit that cast it, or nil")
EI_LUA_FIELD(Cast, target, "object", "the unit it was cast at, or nil (also once it is gone)")
EI_LUA_FIELD(Cast, from, "point", "where the cast started")
EI_LUA_FIELD(Cast, point, "point", "the point it was cast at")
EI_LUA_FIELD(Cast, tick, "integer", "ticks since the cast")
EI_LUA_FIELD(Cast, damage, "number", "the spell's damage (runes included, as every number here)")
EI_LUA_FIELD(Cast, range, "number", "its range")
EI_LUA_FIELD(Cast, radius, "number", "its radius")
EI_LUA_FIELD(Cast, duration, "integer", "its duration in ticks")
EI_LUA_FIELD(Cast, targetCount, "integer", "its number of targets")
EI_LUA_FIELD(Cast, speed, "number", "its speed")
EI_LUA_FIELD(Cast, mana, "number", "its mana cost")
EI_LUA_FIELD(Cast, power, "number", "the game's power curve of its damage, about 0.8 to 1.8")
EI_LUA_FIELD(Cast, areaPower, "number",
             "the same curve of its area over the record's (the fireball's blast is the larger of the two)")
EI_LUA_FIELD(Cast, lightRise, "number",
             "how fast the game's spell lights rise away: the record's `light_range / max(light_rise_divisor, 1)` a "
             "tick")
EI_LUA_FIELD(Cast, lightRange, "number", "the record's `light_range`")
EI_LUA_FIELD(Cast, school, "string", "its school's name")
EI_LUA_FIELD(Cast, silent, "boolean",
             "an item's own periodic cast: the game's buffs end without their sound and its healing makes no sound or "
             "light on a unit at full health")
EI_LUA_FIELD(Cast, data, "table",
             "a table of your own, kept with the cast and in saves: numbers, strings, booleans, units, tables")

// ---- Spells: the cast's methods --------------------------------------------------------------------------------

EI_LUA_CAST_METHOD(FindUnits, "at: position, radius: number, filter?: FindUnitsFilter", "object[]",
                   "the units within `radius` of `at`, nearest first (`filter`: FindUnitsFilter below)")
EI_LUA_CAST_METHOD(CanAffect, "unit: object", "boolean", "the spell's hits would hit the unit (its rune filters)")
EI_LUA_CAST_METHOD(Damage, "unit: object, amount: number, type: string, options?: DamageOptions", "number",
                   "a spell's hit of `amount` of a damage type (`fire`, `lightning`, `acid`, `piercing`, `slashing`, "
                   "`crushing`, `general`) on a unit (other objects are not hit): armour and protections count, the "
                   "caster is the attacker unless `noCredit`. Gives the hit points lost")
EI_LUA_CAST_METHOD(Heal, "unit: object, amount: number", "number",
                   "heals as the healing spell does; gives the hit points gained")
EI_LUA_CAST_METHOD(Apply, "unit: object, kind: string, options?: ApplyOptions", "",
                   "an active effect on the unit: `kind` a spell's code (`slow`, `stun`, `weak`, `prot_fire`, ...: "
                   "that spell's effect and look) or `\"own\"` (this spell's, with its icon, doing what its `effect` "
                   "says: nothing unless given). The values default to the cast's (duration, power, damage, radius)")
EI_LUA_CAST_METHOD(Remove, "unit: object, kind: string", "integer", "the unit's effects of that kind removed; how many")
EI_LUA_CAST_METHOD(Has, "unit: object, kind: string", "boolean", "whether the unit has an effect of that kind")
EI_LUA_CAST_METHOD(Move, "unit: object, to: position, options?: MoveOptions", "boolean",
                   "the unit moved to the point (at its own height) unless it cannot stand there; true when it moved. "
                   "A push is a small move each tick")
EI_LUA_CAST_METHOD(Bolt, "from: position, to: position, options?: BoltOptions", "integer",
                   "a lightning bolt; an end that is a unit follows it. Gives a handle")
EI_LUA_CAST_METHOD(Missile, "options: MissileOptions", "integer",
                   "a spell missile flying at the unit `to` (at the game's missile speed) with the spell's light; "
                   "`OnHit` runs when it lands. Gives a handle")
EI_LUA_CAST_METHOD(Effect, "name: string, options?: EffectOptions", "integer",
                   "a particle effect: one of yours (`Res\\effects`, [Effects](#effects)) or a game's by its script "
                   "name (`Sparks`, `LightningBlast`, `FireBlast`, `FireBall`, `Nuke`, `Healing`, ...). A `free` one "
                   "(a flash that plays once) is not the cast's: it lives its own life, also after the cast ends. "
                   "Gives a handle")
EI_LUA_CAST_METHOD(Sound,
                   "name: string, at: position, volume?: number, hearing?: number; name: string, at: position, "
                   "options: SoundOptions", "", "a sound played once (`magic\\...\\x.wav`), moving with `velocity`")
EI_LUA_CAST_METHOD(SoundLoop, "at: position, loop: string|string[], options?: SoundLoopOptions", "integer",
                   "sounds that go on while the cast holds them: `first` once, then `loop` (a name, or a list played "
                   "in random order) over and over, as the fireball's flight and the walls sound. Gives a handle")
EI_LUA_CAST_METHOD(Light, "at: position, options?: LightOptions", "integer",
                   "a point light, by default in the record's `light_color` and `light_range`, rising by `rise` a "
                   "tick or moving by `velocity`. Gives a handle")
EI_LUA_CAST_METHOD(SetVelocity, "handle: integer, velocity: point", "boolean",
                   "the cast's bolt, missile, effect or light of that handle moves by so much a tick from now on (a "
                   "light sent up as it fades); false when it is gone")
EI_LUA_CAST_METHOD(SetInput, "handle: integer, input: string, value: number|boolean|string|point", "boolean",
                   "an input of the cast's effect of that handle (one of yours, `Res\\effects`) set for every player: a "
                   "number, a flag, a choice by its name or a vector; false when it is gone or has no such input")
EI_LUA_CAST_METHOD(Fire, "handle: integer, event: string", "boolean",
                   "an event input of the cast's effect of that handle fired for every player; false when it is gone or "
                   "has no such event")
EI_LUA_CAST_METHOD(Eye, "unit: object", "point",
                   "the point a unit's spells start from (its eye), where the game's lightning puts its light")
EI_LUA_CAST_METHOD(HitPoint, "unit: object", "point",
                   "the point where a hit on the unit shows, as the game's missiles splash: the ground below it plus "
                   "its height as it stands (lower crawling, lying or dead, so ask after the damage); another "
                   "object's position")
EI_LUA_CAST_METHOD(IsUnit, "object: object", "boolean",
                   "whether it is a unit (false for nil, a unit that is gone, or another object)")
EI_LUA_CAST_METHOD(Ground, "at: position, options?: GroundOptions", "point",
                   "the point on the ground at that place, `above` higher; `water`: on the water's surface where "
                   "there is water, as the game's area spells aim")
EI_LUA_CAST_METHOD(Flight, "from: position, to: position, speed?: number", "Flight",
                   "a flight along the ground at `speed` a second (default the spell's; 5 without one): the ticks it "
                   "takes and the velocity a tick for `Effect`'s `velocity`")
EI_LUA_CAST_METHOD(Release, "handle?: integer", "boolean",
                   "the cast's bolt, missile, effect, light or sound loop of that handle goes now (a missile's "
                   "`OnHit` never runs); false when it is gone or the handle is nil")
EI_LUA_CAST_METHOD(Noise, "at: position, loudness: number, delay?: integer", "",
                   "the caster (a unit; nothing otherwise) makes a noise there that hostile units near it hear and "
                   "come looking for the caster, as the fireball's blast does (`delay` the AI's call delay, 26 unless "
                   "given)")
EI_LUA_CAST_METHOD(Across, "from: position, to: position, length: number", "point",
                   "the vector `{x, y, 0}` `length` long square to the ground line from `from` to `to` (to its left; "
                   "along x when the two stand on one spot), as the game's walls stand across the line from their "
                   "caster to their point: the wall's half from its middle to an end")
EI_LUA_CAST_METHOD(Position, "object: object", "point",
                   "the point where the object is (its own position, not the ground below it), where the teleport's "
                   "departure shows")
EI_LUA_CAST_METHOD(IsDead, "unit: object", "boolean",
                   "whether the unit is dead, as the game's spells test it; false for nil or another object")
EI_LUA_CAST_METHOD(IsHurt, "unit: object", "boolean",
                   "whether the unit's hit points as the players see them are not its maximum (the healing's test); "
                   "false for nil or another object")
EI_LUA_CAST_METHOD(Shout, "unit: object, ack: string", "",
                   "the unit says an ack to its player, by acks.db's names (`\"cant_teleport\"`, `\"link_ended\"`, "
                   "...), as the teleport's caster says it cannot go")
EI_LUA_CAST_METHOD(Reveal, "unit: object", "",
                   "the caster's player sees the unit this tick, as clairvoyance shows the units around its point "
                   "(call it every tick)")
EI_LUA_CAST_METHOD(Wake, "at: position, radius: number", "",
                   "the units within radius of the position wake, as fireworks and clairvoyance wake those around them")
EI_LUA_CAST_METHOD(Notice, "unit: object, at: position, level: integer", "",
                   "the unit's AI notices the position at `level` when that is above what it noticed last: fireworks "
                   "frighten with 300 falling to 50 over their duration")
EI_LUA_CAST_METHOD(Vision, "seer: object, observer: object, options?: VisionOptions", "integer",
                   "the fog of war shows what the unit `seer` sees with the unit `observer`'s senses, as vision fog "
                   "shows its caster what its target sees. Gives a handle")
EI_LUA_CAST_METHOD(Block, "at: position, options: BlockOptions", "",
                   "the ground under a wall from `at - along` to `at + along`, or a disc of `radius` around `at`, "
                   "counts as blocked for the units' paths (they go round it, as round the game's walls and clouds) "
                   "until `Unblock` or the cast's end; one at a time (a second `Block` replaces the first). `Block` "
                   "also works in `OnResume`")
EI_LUA_CAST_METHOD(Unblock, "", "boolean", "the ground `Block` blocked is free again; whether there was one")
EI_LUA_CAST_METHOD(After, "ticks: number, name: string, payload?: any", "",
                   "a timer: the spell file's function of that name runs that many ticks later: `spell.Name(cast, "
                   "payload)`")
EI_LUA_CAST_METHOD(Finish, "", "", "the cast ends after this hook (its bolts, effects and lights go with it)")

// ---- Spells: the methods' options and results ------------------------------------------------------------------

EI_LUA_FIELD(Point, x, "number", "")
EI_LUA_FIELD(Point, y, "number", "")
EI_LUA_FIELD(Point, z, "number", "")
EI_LUA_FIELD(FindUnitsFilter, enemies, "boolean", "only the caster's enemies (with no caster every unit is an enemy)")
EI_LUA_FIELD(FindUnitsFilter, friends, "boolean", "only the caster's friends")
EI_LUA_FIELD(FindUnitsFilter, alive, "boolean", "only living units")
EI_LUA_FIELD(FindUnitsFilter, area, "boolean", "only those area spells hit")
EI_LUA_FIELD(FindUnitsFilter, sight, "boolean", "only those in the line of sight the spell needs from at")
EI_LUA_FIELD(FindUnitsFilter, affect, "boolean", "only those the rune filters let the spell hit")
EI_LUA_FIELD(FindUnitsFilter, exclude, "table<integer, boolean>",
             "a set of ids to leave out: `{[GetObjectID(unit)] = true}`")
EI_LUA_FIELD(FindUnitsFilter, along, "point",
             "a direction: only those within 0.5 of the upright plane through at along it, as the game's walls hit")
EI_LUA_FIELD(FindUnitsFilter, unsorted, "boolean",
             "in the map's own order (the order the game's area spells hit in), not nearest first")
EI_LUA_FIELD(DamageOptions, area, "boolean", "a hit with no direction, as the walls' and clouds'")
EI_LUA_FIELD(DamageOptions, noCredit, "boolean", "the caster is not the attacker")
EI_LUA_FIELD(DamageOptions, split, "number",
             "shares the hit between n targets as the game's area spells do (n above 1: the amount and the hit's "
             "strength x 4 / n; they pass `cast.duration`)")
EI_LUA_FIELD(ApplyOptions, ticks, "integer", "how long it lasts (the cast's duration)")
EI_LUA_FIELD(ApplyOptions, power, "number", "its look's size (the cast's power)")
EI_LUA_FIELD(ApplyOptions, value, "number", "its value, `effect.value` in `OnStats` (the cast's damage)")
EI_LUA_FIELD(ApplyOptions, strength, "number", "its strength (the cast's radius)")
EI_LUA_FIELD(MoveOptions, check, "boolean", "only to a place the unit can stand on (true)")
EI_LUA_FIELD(BoltOptions, width, "number", "the bolt's width")
EI_LUA_FIELD(BoltOptions, lifetime, "integer", "ticks it lasts (else as long as the cast)")
EI_LUA_FIELD(BoltOptions, demon, "boolean", "the curse magic's look")
EI_LUA_FIELD(MissileOptions, to, "object", "the unit it flies at")
EI_LUA_FIELD(MissileOptions, from, "position", "where it starts (the caster)")
EI_LUA_FIELD(MissileOptions, effect, "string", "its particle effect (FireArrow)")
EI_LUA_FIELD(MissileOptions, size, "number", "the effect's size (the cast's power)")
EI_LUA_FIELD(MissileOptions, sound, "string", "the sound it makes in flight")
EI_LUA_FIELD(MissileOptions, payload, "any", "given to OnHit")
EI_LUA_FIELD(EffectOptions, at, "position", "where it shows (the cast's point)")
EI_LUA_FIELD(EffectOptions, attach, "object", "the unit it shows on")
EI_LUA_FIELD(EffectOptions, size, "number", "its size (1)")
EI_LUA_FIELD(EffectOptions, alpha, "number", "its opacity (1)")
EI_LUA_FIELD(EffectOptions, param, "number",
             "its source's parameter (the fire blast's spread: the fireball passes its radius)")
EI_LUA_FIELD(EffectOptions, velocity, "point", "it flies by so much a tick")
EI_LUA_FIELD(EffectOptions, target, "position", "a flying one heads for this point")
EI_LUA_FIELD(EffectOptions, unit, "object",
             "a flying one starts on the players' screens at this unit's hand (the fireball)")
EI_LUA_FIELD(EffectOptions, direction, "point",
             "a wall effect's ([server] wall: Tendril, Link) extent from its middle to an end (the fire wall's)")
EI_LUA_FIELD(EffectOptions, lifetime, "integer", "ticks it lasts (else as long as the cast)")
EI_LUA_FIELD(EffectOptions, free, "boolean", "not the cast's: it lives its own life")
EI_LUA_FIELD(SoundOptions, volume, "number", "its volume")
EI_LUA_FIELD(SoundOptions, hearing, "number", "how far it is heard")
EI_LUA_FIELD(SoundOptions, velocity, "point", "it moves by so much a tick")
EI_LUA_FIELD(SoundLoopOptions, first, "string", "the sound played once before the loop")
EI_LUA_FIELD(SoundLoopOptions, volume, "number", "their volume")
EI_LUA_FIELD(SoundLoopOptions, hearing, "number", "how far they are heard")
EI_LUA_FIELD(SoundLoopOptions, velocity, "point", "they move by so much a tick")
EI_LUA_FIELD(SoundLoopOptions, lifetime, "integer", "ticks they last (else as long as the cast)")
EI_LUA_FIELD(LightOptions, color, "number[]", "{r, g, b} (the record's light_color)")
EI_LUA_FIELD(LightOptions, range, "number", "its range (the record's light_range)")
EI_LUA_FIELD(LightOptions, rise, "number", "it rises by so much a tick")
EI_LUA_FIELD(LightOptions, velocity, "point", "it moves by so much a tick")
EI_LUA_FIELD(LightOptions, lifetime, "integer", "ticks it lasts (else as long as the cast)")
EI_LUA_FIELD(GroundOptions, above, "number", "so much higher")
EI_LUA_FIELD(GroundOptions, water, "boolean", "on the water's surface where there is water")
EI_LUA_FIELD(VisionOptions, lifetime, "integer", "ticks it lasts (else as long as the cast)")
EI_LUA_FIELD(BlockOptions, along, "point", "the wall's half: from `at - along` to `at + along`")
EI_LUA_FIELD(BlockOptions, radius, "number", "a disc of that radius instead")
EI_LUA_FIELD(Flight, ticks, "integer", "the ticks the flight takes")
EI_LUA_FIELD(Flight, distance, "number", "its length along the ground")
EI_LUA_FIELD(Flight, speed, "number", "its speed a second")
EI_LUA_FIELD(Flight, velocity, "point", "its velocity a tick, for Effect's velocity")

// ---- Active effects: OnStats' values ---------------------------------------------------------------------------

EI_LUA_FIELD(Effect, kind, "string", "its spell's code (lower case)")
EI_LUA_FIELD(Effect, value, "number",
             "the strongest value of its kind's effects on the unit (from 0; the spell's damage unless `cast:Apply` "
             "gave one)")
EI_LUA_STATS_METHOD(Add, "name: stat, x: number", "", "the total plus `x`")
EI_LUA_STATS_METHOD(Subtract, "name: stat, x: number", "", "the total minus `x`")
EI_LUA_STATS_METHOD(Larger, "name: stat, x: number", "",
                    "the larger of the total and `x`: how a protection and antimagic's take the larger of the two")
EI_LUA_STATS_METHOD(Multiply, "name: stat, x: number", "", "the total times `x`")
EI_LUA_STAT(FireProtection, "added to the unit's protection from fire (the protection spells and antimagic: `Larger`)")
EI_LUA_STAT(LightningProtection, "added to the unit's protection from lightning")
EI_LUA_STAT(AcidProtection, "added to the unit's protection from acid")
EI_LUA_STAT(Sight, "added to the sight range (eagle sight)")
EI_LUA_STAT(DaySight, "added to the sight's percentage by day (infravision)")
EI_LUA_STAT(NearSense, "added to the range sensed whatever the light (detect life)")
EI_LUA_STAT(SightConspicuity,
            "added to how visible the unit is, which stays at least 0 (invisibility subtracts its value in percent)")
EI_LUA_STAT(HearingConspicuity, "added to how loud the unit is, at least 0 (silence subtracts its value)")
EI_LUA_STAT(NearConspicuity, "added to how near the unit is sensed, at least 0 (lichdom subtracts its value)")
EI_LUA_STAT(SmellConspicuity, "added to how the unit smells (stench)")
EI_LUA_STAT(Haste, "added to the action rate, which stays at least 1 (speed adds its value, slow subtracts its)")
EI_LUA_STAT(Might,
            "hit points times (1 + `Might` x 0.005) / (1 + `Frailty` x 0.005), at least 1, and the unit's damage the "
            "same with 0.003 (strength)")
EI_LUA_STAT(Frailty, "the divisor of `Might`'s formula (weak)")
EI_LUA_STAT(Regeneration, "while above 0, the hit points regenerate that many times as fast (regeneration)")
EI_LUA_STAT(Girth,
            "the figure grows by 0.4 a step: with `Size` 0, `Girth` steps wider (for the hero only in width), else "
            "`Size` + `Girth` steps on every axis (strength and weak 1 and -1)")
EI_LUA_STAT(Size, "steps of the figure's size on every axis (enlarge and shrink 1 and -1)")
EI_LUA_STAT(Stunned, "while above 0 the unit is stunned: no actions (stun; the teleport's arrival)")
EI_LUA_STAT(NoCasting, "while above 0 the unit casts no spells, unless its natural weapon is spells only (feeblemind)")
EI_LUA_STAT(Strength,
            "added to the strength (after the skill levels'): with it the hit points, the carried weight and the "
            "skills it leads (no game buff touches this one or those below)")
EI_LUA_STAT(Dexterity, "added to the dexterity: with it the mana, the action rate and the skills it leads")
EI_LUA_STAT(Intelligence, "added to the intelligence: with it the experience gained and the magic skills")
EI_LUA_STAT(MaxHitPoints, "added to the hit point maximum, before `Might`'s scale")
EI_LUA_STAT(MaxMana,
            "added to the mana maximum of a hero or a named unit (another unit's is its prototype's, unchanged)")
EI_LUA_STAT(HitPointRegen,
            "added to the share of the maximum the hit points regenerate every 15 ticks while hurt (0.005: half a "
            "percent), before `Regeneration`'s factor")
EI_LUA_STAT(ManaRegen, "added to the share of the maximum the mana regenerates every 15 ticks out of a fight")
EI_LUA_STAT(PiercingProtection, "added to the unit's protection from piercing")
EI_LUA_STAT(SlashingProtection, "added to the unit's protection from slashing")
EI_LUA_STAT(CrushingProtection, "added to the unit's protection from crushing")
EI_LUA_STAT(GeneralProtection, "added to the unit's protection from general damage")
EI_LUA_STAT(Hearing, "added to the hearing range")
EI_LUA_STAT(Smell, "added to the sense of smell")
EI_LUA_STAT(Attack, "added to the attack of the unit's blows and shots (whole numbers count)")
EI_LUA_STAT(Defence, "added to the defence against blows (whole numbers count; none against shots)")
EI_LUA_STAT(Damage, "added to the damage of a weapon's or a natural attack's hit, before `Might`'s scale")
EI_LUA_STAT(Speed, "added to the movement speed, which stays at least 0")
EI_LUA_STAT(MeleeSkill, "added to the melee skill (the attack and defence with a blade, an axe, a club, ...)")
EI_LUA_STAT(ArcherySkill, "added to the archery skill (bows and crossbows)")
EI_LUA_STAT(ElementalSkill, "added to the elemental magic skill (fire, lightning, acid: what spells it may cast)")
EI_LUA_STAT(SenseSkill, "added to the sense magic skill (illusion, divination)")
EI_LUA_STAT(AstralSkill, "added to the astral magic skill (enchantment, healing, domination)")
EI_LUA_STAT(Science, "added to the science (the Use and Steal skills)")
EI_LUA_STAT(Taming, "added to the taming")

// ---- Active effects: the one-shot hooks' values ----------------------------------------------------------------

EI_LUA_FIELD(ActiveEffect, kind, "string", "its spell's code (lower case)")
EI_LUA_FIELD(ActiveEffect, value, "number", "its value (the spell's damage unless `cast:Apply` gave one)")
EI_LUA_FIELD(ActiveEffect, ticks, "integer", "the ticks it has left")
EI_LUA_FIELD(ActiveEffect, power, "number", "its look's size (its cast's power)")
EI_LUA_EFFECT_METHOD(Damage, "amount: number, type: string, options?: DamageOptions", "number",
                     "a hit of `amount` of a damage type on the unit, as `cast:Damage`'s without a caster (no one "
                     "has the credit; `noCredit` is not read). Gives the hit points lost")
EI_LUA_EFFECT_METHOD(Heal, "amount: number", "number", "heals the unit as `cast:Heal` does; gives the hit points gained")
EI_LUA_EFFECT_METHOD(Remove, "", "boolean",
                     "the effect taken off the unit now (its `OnEffectEnded` follows, `\"removed\"`); whether it was "
                     "still on")

#undef EI_LUA_SECTION
#undef EI_LUA_BINDING
#undef EI_LUA_GLOBAL
#undef EI_LUA_HELPER
#undef EI_LUA_CAST_METHOD
#undef EI_LUA_STATS_METHOD
#undef EI_LUA_EFFECT_METHOD
#undef EI_LUA_HOOK
#undef EI_LUA_FIELD
#undef EI_LUA_STAT
