#pragma once

// The console's command maps as a registered stlp::TreeAbi instantiation (console_command_map.cpp).

#include "console.h"

#include "lib/stlp_tree_abi.h"

namespace console {

using CommandMapAbi = stlp::TreeAbi<CommandMap>;

} // namespace console
