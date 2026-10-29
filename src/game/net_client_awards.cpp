// The game client's tick and its award handling: timers count down,
// and received awards are applied to the local player. See net_classes.h and docs/notes/network.md.

#include "core_refcount.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "net_messages.h"
#include "party.h"
#include "player_messages.h"
#include "world.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace net {

namespace {

// Player 0 of the world (the local player), or null.
CPlayer* LocalPlayer() { return g_serverWorld.GetPlayer(0); }

} // namespace

// Applies the received awards (net_server_awards.cpp) to player 0 and empties the list. Kind 0: experience; 1:
// money (the party's, the value truncated), then an empty item list given; 2: the item named by the text, given; 3:
// the first given item whose IsSameAs with the named item returns 0 is taken (by id); 4: the party's quest items
// removed and every given quest item (class 0x14) taken.
void ClientSession::ApplyAwards() {
    ClientSession* self = this;
    CPlayer* localPlayer = LocalPlayer();
    if (!localPlayer) return;
    for (auto& node : self->awards) {
        auto* award = node.Get();
        const char* name = award->text.data();
        switch (award->kind) {
        case 0: {
            std::uint32_t bits;
            bits = std::bit_cast<std::uint32_t>(award->value);
            g_serverWorld.AwardExperience(nullptr, std::bit_cast<float>(bits));
            break;
        }
        case 1: {
            std::vector<Item*> none;
            // Truncated as _ftol does: through 64 bits, so a NaN or a value beyond the int range gives 0.
            localPlayer->Party()->money += static_cast<int>(static_cast<std::int64_t>(award->value));
            localPlayer->GiveItems(&none, 0);
            break;
        }
        case 2: {
            std::vector<Item*> items;
            Item* item = localPlayer->Party()->ItemFactory()->CreateItem(name);
            if (item) item->AddRef();
            items.push_back(item);
            localPlayer->GiveItems(&items, 0);
            ReleaseAndClear(items);
            break;
        }
        case 3: {
            auto* item = localPlayer->Party()->ItemFactory()->CreateItem(name);
            if (item) item->AddRef();
            for (Item* owned : localPlayer->items) {
                if (!owned->IsSameAs(item)) {
                    std::int32_t id = owned->id;
                    if (id >= 0) localPlayer->TakeItem(id);   // (the walk ends here)
                    break;
                }
            }
            if (item) item->Release();
            break;
        }
        case 4: {
            localPlayer->Party()->RemoveQuestItems();
            // TakeItem erases from the list: the quest items' ids first.
            std::vector<int> quest;
            for (Item* owned : localPlayer->items)
                if (owned->GetClassId() == ClassId::ItemQuest) quest.push_back(owned->id);
            for (int id : quest) localPlayer->TakeItem(id);
            break;
        }
        default:
            break;
        }
    }

    // The list emptied inline: each award released, each node back to the 16-byte pool list.
    self->awards.clear();
}

// The client base tick, then the timers count down by `dt` (as a float) and those below zero are dropped. Awards
// are applied in a game of mode 3 when player 0 exists with its connection in state 1 and the game-flow flag is set.
void ClientSession::Tick(double dt) {
    ClientSession* self = this;
    self->ClientBase::Tick(dt);
    const float step = static_cast<float>(dt);
    for (auto node = self->timers.begin(); node != self->timers.end();) {
        float& seconds = node->seconds;
        seconds -= step;
        EI_NAN_AUDIT(seconds);
        if (seconds < 0.0f) {
            node = self->timers.erase(node);
        } else {
            ++node;
        }
    }
    if (self->awards.begin() == self->awards.end() || g_serverWorld.sessionKind != 3) return;
    CPlayer* localPlayer = LocalPlayer();
    if (!localPlayer || localPlayer->connection->state != 1 || !g_serverWorld.zoneLoaded) return;
    ApplyAwards();
}

} // namespace net
