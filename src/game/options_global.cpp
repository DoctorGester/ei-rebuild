// The game's options object, zero at the start: its static initialiser builds it in the list's order
// and the exit function it registers destroys it (options_construct.cpp). The definition is on its own so the
// vector replay links it without the constructor.
#include "options.h"

namespace options {
constinit lib::StaticObject<options::GameOptions> g_options = /*0079BC70*/ {};
} // namespace options
