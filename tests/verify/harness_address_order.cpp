// The "checkplayers" step (harness_address_order.h; ticket UB1).
#include "harness_address_order.h"

#include "harness_windows.h"
#include "image/image_side.h"

#include "../../src/hook/log.h"

#include <cstdio>

namespace ei::verify::address_order {

bool CheckPlayerList(std::string* text) {
    void* top = TopWindow();
    image::PlayerList list;
    char line[200];
    bool ok = false;
    if (!top || WindowKind(top) != "briefing") {
        std::snprintf(line, sizeof(line), "the base screen is not on top");
    } else if (!image::BriefingPlayerList(top, &list)) {
        std::snprintf(line, sizeof(line), "no network client, or its base screen has no player list");
    } else {
        ok = list.bound && list.players > 0 && list.listed == list.players;
        std::snprintf(line, sizeof(line), "the base screen lists %d of the client's %d players (binding %s)", list.listed,
                      list.players, list.bound ? "live" : "missing");
    }
    *text = line;
    Log("harness: checkplayers: %s: %s", line, ok ? "ok" : "FAIL");
    return ok;
}

} // namespace ei::verify::address_order
