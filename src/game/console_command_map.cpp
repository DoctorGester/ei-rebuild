// The console's command maps (STLport map<CString, CommandHandler>, console.h CommandMap; the two
// globals PublicCommands / CheatCommands) as a registered instantiation (stlp::TreeAbi): the
// members the standalone boot reaches (ticket S16; the rest of the family is S12a's). Keys are
// ordered by mfc::CString's operator< (_mbscmp). Checked by tests/verify/verify_console_command_map.cpp.
#include "console_command_map.h"

#include "ei/hook.h"

namespace console {

// 005E0A90: map() (the static initialisers 005DC050 / 005DC120 build both maps with it).
EI_REPLACE(0x005E0A90, CommandMapAbi::Construct);
// 005DC080: ~map (the exit functions 005DC070 / 005DC140): clear, then the header back to the pool.
EI_REPLACE(0x005DC080, CommandMapAbi::Destroy);
// 005E0E40 (RET 4): _M_erase.
EI_REPLACE(0x005E0E40, CommandMapAbi::EraseSubtree);
// 005E0B20 (RET 4): operator[]: lower_bound, then (key, null) inserted at it when absent; the
// handler slot.
EI_REPLACE(0x005E0B20, CommandMapAbi::Subscript);

} // namespace console
