// The exit function that does nothing: what the original's one-byte `ret` exit functions do (docs/notes/standalone.md,
// "Short functions"). The static initialisers (static_init.cpp) and the function-local statics register it with
// atexit (at_exit_functions.h).

namespace ei::short_functions {

void Ret() {}

} // namespace ei::short_functions
