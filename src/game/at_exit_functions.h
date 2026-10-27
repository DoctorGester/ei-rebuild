#pragma once

// The exit functions that function-local statics register with atexit (libcrt::AtExit)
// and that have a body in our code, by the file that holds it: the generated ones of src/standalone/static_exit.cpp
// (named after the address they replace) and the empty one (short_functions.cpp). Registering the
// function itself is what the original's call does through its address: the hook build patches
// that address to jump here.

namespace ei::static_init {
void __cdecl Exit004E1C50();   // the dead fog cells (effects_weather.cpp)
void __cdecl Exit004E2280();   // the weather draw's records (effects_weather_draw.cpp)
} // namespace ei::static_init

namespace ei::short_functions {
void Ret();   // nothing to destroy (the wake cache)
} // namespace ei::short_functions
