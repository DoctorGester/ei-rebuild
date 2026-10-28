// The game client's tick and its award handling: timers count down,
// and received awards are applied to the local player. See net_classes.h and docs/notes/network.md.

#include "net_messages.h"
#include "core_refcount.h"
#include "item.h"
#include "party.h"
#include "player_messages.h"

#include "lib/crt.h"
#include <bit>
#include <cstdint>
#include <cstring>

#include "world.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <list>

namespace net {

namespace {

constexpr int kClassQuestItem = 0x14;

// Player 0 of the world (the local player), or null.
CPlayer* LocalPlayer() { return g_serverWorld->GetPlayer(0); }

} // namespace

// Applies the received awards (net_server_awards.cpp) to player 0 and empties the list. Kind 0: experience; 1:
// money (the party's, the value truncated), then an empty item list given; 2: the item named by the text, given; 3:
// the first given item whose IsSameAs with the named item returns 0 is taken (by id); 4: the party's quest items
// removed and every given quest item (class 0x14) taken.
void ClientSession::ApplyAwards() {
    ClientSession* self = this;
    CPlayer* player = LocalPlayer();
    if (!player) return;
    for (auto node = self->awards_.begin(); node != self->awards_.end(); ++node) {
        auto* award = node->Get();
        const char* name = award->text_.data();
        switch (award->kind_) {
        case 0: {
            std::uint32_t bits;
            bits = std::bit_cast<std::uint32_t>(award->value_);
            g_serverWorld->AwardExperience(nullptr, std::bit_cast<float>(bits));
            break;
        }
        case 1: {
            std::list<CItem*> none;
            player->Party()->money_ += static_cast<int>(libcrt::Ftol(award->value_));
            player->GiveItems(&none, 0);
            none.clear();
            break;
        }
        case 2: {
            std::list<CItem*> items;
            CItem* item = player->Party()->ItemFactory()->CreateItem(name);
            HeldObject<CItem>::Of(&items.emplace_back())->Assign(item);
            player->GiveItems(&items, 0);
            ReleaseAndClear(items);
            break;
        }
        case 3: {
            auto* item = player->Party()->ItemFactory()->CreateItem(name);
            if (item) ++item->refCount_;
            for (auto position = player->items_.begin(); position != player->items_.end();) {
                std::list<CItem*>::iterator  current = position;
                ++position;
                CItem* owned = *current;
                if (!owned->IsSameAs(item)) {
                    std::int32_t id = owned->id_;
                    if (id >= 0) player->TakeItem(id);
                    break;
                }
            }
            if (item) item->Release();
            break;
        }
        case 4:
            player->Party()->RemoveQuestItems();
            for (auto position = player->items_.begin(); position != player->items_.end();) {
                std::list<CItem*>::iterator  current = position;
                ++position;
                CEngineObject* owned = *current;
                if (owned->GetClassId() == kClassQuestItem) player->TakeItem(owned->id_);
            }
            break;
        default:
            break;
        }
    }

    // The list emptied inline: each award released, each node back to the 16-byte pool list.
    self->awards_.clear();
}

// The client base tick, then the timers count down by `dt` (as a float) and those below zero are dropped. Awards
// are applied in a game of mode 3 when player 0 exists with its connection in state 1 and the game-flow flag is set.
void ClientSession::Tick(double dt) {
    ClientSession* self = this;
    self->ClientBase::Tick(dt);
    auto sentinel = self->timers_.end();
    auto node = self->timers_.begin();
    if (node != sentinel) {
        const float step = static_cast<float>(dt);
        do {
            auto current = node;
            ++node;
            float& seconds = current->seconds;
            const double left = static_cast<double>(seconds) - step;
            seconds = static_cast<float>(left);
            EI_NAN_AUDIT(left);
            if (left < 0.0f) self->timers_.erase(current);
        } while (node != sentinel);
    }
    if (self->awards_.begin() == self->awards_.end() || g_serverWorld->sessionKind_ != 3) return;
    CPlayer* player = LocalPlayer();
    if (!player || player->connection_->state_ != 1 || !g_serverWorld->zoneLoaded_) return;
    ApplyAwards();
}

} // namespace net
