// The effects' Lua API (docs/notes/effects.md, "Lua hooks"): the hooks an effect's script may hold and the methods of
// the two values they get, once. particle_lua.cpp registers the methods from these rows; game/script/script_lua_api.h
// takes them among the game's rows, so ei-convert --lua-api writes them into the LuaLS definition file and
// docs/modding.md's tables (tools/convert/lua_api.cpp), and ei-convert --lua-check holds the state to them.
//
// EI_EFFECT_LUA_HOOK(name, params, returns, text)            a function the script's table may hold
// EI_EFFECT_LUA_SOURCE_METHOD(name, params, returns, text)   e:name, effectlua's Source_name (particle_lua.cpp)
// EI_EFFECT_LUA_PARTICLE_METHOD(name, params, returns, text) p:name, effectlua's Particle_name
//
// params and returns as in game/script/script_lua_api.inc; the types `source` (ei.EffectSource) and `particle`
// (ei.Particle).

#ifndef EI_EFFECT_LUA_HOOK
#define EI_EFFECT_LUA_HOOK(name, params, returns, text)
#endif
#ifndef EI_EFFECT_LUA_SOURCE_METHOD
#define EI_EFFECT_LUA_SOURCE_METHOD(name, params, returns, text)
#endif
#ifndef EI_EFFECT_LUA_PARTICLE_METHOD
#define EI_EFFECT_LUA_PARTICLE_METHOD(name, params, returns, text)
#endif

EI_EFFECT_LUA_HOOK(on_start, "e: source", "",
                   "once, at the effect's first tick, before its data starts (its vars, its first stage): inputs set "
                   "here are what the start reads")
EI_EFFECT_LUA_HOOK(on_tick, "e: source, age: integer", "",
                   "every tick (the first too, after `on_start`), before the data's step; `age` counts the ticks from 0")
EI_EFFECT_LUA_HOOK(on_event, "e: source, name: string, count: integer", "",
                   "after `on_tick`, for each event input fired since the last tick (`count`: how many times), "
                   "`\"stop\"` when the game stopped the emission and `\"spawn\"` when the game's effect object put "
                   "the effect into the world (count 1); `\"end\"` once, after the step in which the effect ended")
EI_EFFECT_LUA_HOOK(on_spawn, "e: source, p: particle, index: integer", "",
                   "each particle the data made, once it is made (`index`: its number in this tick's emission); "
                   "within the particle budget")
EI_EFFECT_LUA_HOOK(on_update, "e: source, p: particle", "",
                   "opt-in: each live particle, after the data moved it; within the particle budget, past it the "
                   "data alone moves it")

EI_EFFECT_LUA_SOURCE_METHOD(set, "input: string, value: number|boolean|string; input: string, x: number, y: number, "
                                 "z: number; input: string, points: point[]",
                            "boolean",
                            "sets an input: a float, bool or enum (by its value's name or its index), a vector, a "
                            "list's points; false when the effect has no such input of that type")
EI_EFFECT_LUA_SOURCE_METHOD(fire, "event: string", "boolean",
                            "fires an event input (its reactions run in this tick's step when fired before it, else at "
                            "the next); false when the effect has none of that name")
EI_EFFECT_LUA_SOURCE_METHOD(get, "name: string", "any",
                            "a value of the effect: an input (a vector's x, y, z; an enum's value name; a list's count), "
                            "a var (its numbers), `stage` (its name, nil before the first) or a source value as the "
                            "expressions name it (`age`, `size`, `origin` (x, y, z), `count`, `carrier_radius`, ...)")
EI_EFFECT_LUA_SOURCE_METHOD(random, "lo?: number, hi?: number", "number",
                            "a random number in [lo, hi) (without them in [0, 1)) from the particles' own generator")
EI_EFFECT_LUA_SOURCE_METHOD(pick, "n: integer", "integer", "a random whole number from 0 to n - 1, the same way")
EI_EFFECT_LUA_SOURCE_METHOD(ground, "x: number, y: number", "number", "the ground's height at the point")
EI_EFFECT_LUA_SOURCE_METHOD(effect, "name: string, x?: number, y?: number, z?: number", "",
                            "starts another effect, free, at the source's origin or at the point")

EI_EFFECT_LUA_PARTICLE_METHOD(get, "name: string", "any",
                              "`position`, `velocity` (x, y, z), `size`, `alpha` (0..255), `frame`, `color` (r, g, b, "
                              "0..255 each), `age`, `life` (ticks left), `kind` (its kind's name) or a var of its kind "
                              "(its numbers)")
EI_EFFECT_LUA_PARTICLE_METHOD(set, "name: string, ...: number", "",
                              "sets `position` or `velocity` (x, y, z), `size`, `alpha`, `frame`, `color` (r, g, b), "
                              "`life` or a var of its kind")
EI_EFFECT_LUA_PARTICLE_METHOD(kill, "", "",
                              "the particle dies at this update (its `on_death` runs); in `on_spawn` it is not kept")

#undef EI_EFFECT_LUA_HOOK
#undef EI_EFFECT_LUA_SOURCE_METHOD
#undef EI_EFFECT_LUA_PARTICLE_METHOD
