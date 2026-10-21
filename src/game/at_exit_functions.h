#pragma once

// The exit functions that function-local statics register with atexit (GameAtExit, originals_library.h)
// and that have a body in our code, by the file that holds it: the hand-written destructors of
// 00400000-00440000's globals (atexit_400.cpp), the generated ones of src/standalone/static_exit.cpp
// (named after the address they replace) and the empty one (short_functions.cpp). Registering the
// function itself is what the original's call does through its address: the hook build patches
// that address to jump here.

namespace atexit400 {
void __cdecl DestroyGlobalVector();   // 00434980: the vector at 0079B8D0
} // namespace atexit400

namespace ei::static_init {
void __cdecl Exit004E1C50();   // the dead fog cells (effects_weather.cpp)
void __cdecl Exit005D2570();   // Aggression::Evaluate's candidate list (unit_ai_modes.cpp)
void __cdecl Exit0058DA00();   // the sleeping ids of CWorldServer::UpdateUnits (world_tick.cpp)
void __cdecl Exit0058DA20();   // its removed ids
void __cdecl Exit006C9270();   // Terrain::SetSun's sorted sectors (terrain_animation.cpp)
} // namespace ei::static_init

namespace ei::short_functions {
void Ret();   // 0058D5C0 among others: nothing to destroy (the wake cache)
} // namespace ei::short_functions
