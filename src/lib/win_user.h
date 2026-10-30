#pragma once

// winuser.h's constants for a file that uses them as the game's own values without calling Windows: the virtual-key
// codes (VK_*: the game's key codes are Windows') and DrawText's format flags (DT_*: the interface's text layout takes
// them). winuser.h over lib/win_types.h (~0.1 s of a file) rather than <windows.h> (~0.4 s). A file that calls
// Windows includes <windows.h>.

#include "lib/win_types.h"

#include <winuser.h>
