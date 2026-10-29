// The base screen's player list in the image (image_side.h; harness_address_order.cpp, CheckPlayerList).
#include "image_side.h"

#include "game/net_classes.h"
#include "game/net_messages.h"
#include "game/ui_briefing.h"

namespace ei::verify::image {

bool BriefingPlayerList(void* briefing, PlayerList* list) {
    const net::ClientSession* client = net::g_gameClient;
    const CChatWindow* chat = static_cast<CBriefingScreen*>(briefing)->chat;
    if (!client || !chat) return false;
    list->players = static_cast<int>(client->players.size());
    const CRefObject* binding = client->binding;
    list->bound = binding && !(binding->flags7 & CRefObject::kDetached);
    list->listed = chat->listed;
    return true;
}

} // namespace ei::verify::image
