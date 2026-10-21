// The game's options object (0079BC70), zero at the start: the static initialiser 00458700 builds it in the list's order
// and the exit function 00458720 it registers destroys it (options_construct.cpp). The definition is on its own so the
// vector replay links it without the constructor.
#include "options.h"

static_assert(sizeof(options::GameOptions) == 0x26C);

namespace options {
constinit lib::StaticObject<options::GameOptions> g_options = /*0079BC70*/ {};
} // namespace options
