#pragma once

// The exit functions that function-local statics register with atexit (std::atexit) and that have a body
// in our code, by the file that holds it: the generated ones of src/standalone/static_exit.cpp (named after
// the address they replace). Registering the function itself is what the original's call
// does through its address: the hook build patches that address to jump here.

namespace ei::static_init {
void Exit004E1C50();   // the dead fog cells
void Exit004E2280();   // the weather draw's records
} // namespace ei::static_init
