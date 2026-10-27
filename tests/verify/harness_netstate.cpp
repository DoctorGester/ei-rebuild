// The "netstate" step: the sessions' state as console lines (harness_pair.h, SV5d4).
#include "harness_pair.h"

#include "exe_objects.h"
#include "harness_netstate_read.h"
#include "harness_original.h"
#include "image/image_side.h"

#include "ei/hook.h"

namespace ei::verify::pair {

// Ours in the image; in --no-hooks runs game.exe's own sessions (0079B5DC, 0079B5D8), which the original code uses.
std::vector<std::string> NetState() {
    if (ei::Standalone()) return image::NetState();
    return NetLines<exe::net::ServerEntry>(ei::OriginalGameData<exe::net::ServerSession*>(0x0079B5DC),
                                           ei::OriginalGameData<exe::net::ClientSession*>(0x0079B5D8));
}

} // namespace ei::verify::pair
