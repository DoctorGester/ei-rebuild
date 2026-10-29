// A network client's player list on the base screen (CChatWindow). See
// ui_briefing.h and docs/notes/ui.md, section 3. Multiplayer only.
#include "game_data.h"
#include "game_singletons.h"
#include "item_db_text.h"
#include "net_classes.h"
#include "net_messages.h"
#include "plugin_fixes.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "unit_db.h"
#include "world_glue.h"

#include "lib/str.h"
#include "lib/vecmath.h"

#include "ei/rebirth.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

// The status texts' keys, in texts_ order.
constexpr const char* kTextKeys[] = {
    "lmp_status_zone",
    "lmp_status_base",
    "lmp_status_camp",
    "lmp_status_connect",
    "lmp_exch_we_offer",
    "lmp_exch_they_offer",
};
constexpr int kWeOffer = 4, kTheyOffer = 5;

constexpr const char* kOnOffSound = "buttons\\battle\\on_off.wav";

constexpr int kSurfaceFormat = 5;
constexpr int kTextureKind = 0x20;
constexpr int kFaceWidth = 0x38;
constexpr int kFirstFaceLeft = 0x2E8;
constexpr int kFirstStatusLeft = 0x118;   // with the window at 0x1D0; 0x2E8 with EI_Plugin's NPlayers
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kPickCommandA = 4, kPickCommandB = 5;   // the client's command kinds for picks 1 / 2

net::PlayerMessage* PlayerOf(CRefObject* object) { return static_cast<net::PlayerMessage*>(object); }
bool Live(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }

} // namespace

// --- the constructor -----------------------------------------------------------------------------

// Its own update; the input side's click and mouse move.
CChatWindow::CChatWindow(CInterface3D* ui) : CUiWindow(ui) {
    // EI_Plugin's NPlayers: the window and its surface start at x 0, not 0x1D0.
    const int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? 0 : 0x1D0;
    rect_ = RECT{left, 0, 800, 0xA0};
    CUiSurface* surface = new CUiSurface(interface_, left, 0x4B, 800, 0x69, kSurfaceFormat, 1);
    AddSurface(surface);
    portraits_ = 0;
    std::string text;
    for (const char* key : kTextKeys) {
        g_clientWorld->texts_->GetString(key, &text);
        texts_.push_back(text);
    }
}
// EI_Plugin.asi patches the constructor and Update (NPlayers: the window's left edge and the first status
// column) and ShowPlayers (Fixes: the faces' sex and the prototype fallback) in a Rebirth launch.

// --- a face --------------------------------------------------------------------------------------

void CChatWindow::AddPortrait(const std::string& model, std::string_view texture) {
    // Four faces share a 256 x 256 texture, one quarter each.
    int u = 0, v = 0;
    switch (portraits_ % 4) {
    case 0: CreateTexture(0x100, 0x100, kTextureKind); break;
    case 1: u = 0x80; break;
    case 2: v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: break;   // a negative count: u is unset and `texture` is used as v
    }
    CopyImage(objects_[std::ssize(objects_) - 1], texture, 0x80, 0x80, 0x80, 0x80, u, v);

    CUiMesh* sprite = AddSprite(model);
    int left = kFirstFaceLeft - portraits_ * kFaceWidth;
    int right = left + kFaceWidth;
    if (sprite) {
        sprite->Place(static_cast<float>(left + right) * 0.5f, 32.0f, 7.0f);
        sprite->SetScale(0.3f);
        Vector3 axis{1.0f, 0.0f, 0.0f};
        Quaternion turn;
        // pi * 1.05, passed as a float.
        sprite->localRotation_ =
            *turn.SetAxisAngle(axis, 3.1415927f * 1.05f);
    } else {
        sprite = AddBlankSprite();
        sprite->SetRectangle(7.0f, 100.0f, static_cast<float>(left), 0.0f, static_cast<float>(right), 60.0f, 0, 0, 0x80,
                             0x80);
    }
    sprite->textureOffsetU_ = static_cast<float>(u) * 0.00390625f;
    sprite->textureOffsetV_ = static_cast<float>(v + 0x80) * 0.00390625f;
    sprite->texture_ = objects_[std::ssize(objects_) - 1];
    ++portraits_;
    AddRegion()->SetRect(left, 0, right, 0x50);
}

// --- the players' faces --------------------------------------------------------------------------

void CChatWindow::ShowPlayers() {
    ClearSceneObjects();
    ClearRegions();
    ClearObjects();
    portraits_ = 0;
    listed_ = 0;

    players_.clear();

    // Our own player first, the others after it; none while the binding is gone.
    net::ClientSession* client = net::g_gameClient;
    const auto& players = client->players_;
    for (auto node = players.begin(); node != players.end(); ++node) {
        if (!Live(client->binding_)) continue;
        CRefObject* player = node->ptr;
        bool own = client->binding_->PlayerRef().ptr == player;
        if (own)
            players_.emplace(players_.begin(), player);
        else
            players_.emplace_back(player);
        ++listed_;
    }

    // The faces (the strings live across players: the sex letter carries over).
    std::string model, picture, sex, build;
    for (auto& p : players_) {
        net::PlayerMessage* player = PlayerOf(p);
        UnitPrototypeRecord* prototype;
        {
            prototype = unitdb::PrototypeNamed(std::string(player->text_));
            // EI_Plugin's Fixes: an unknown prototype becomes the first one that is no creature.
            if (!prototype && pluginfixes::Enabled())
                prototype = pluginfixes::FallbackPrototype();
        }
        UnitModelRecord* figure;
        {
            std::string key(prototype->modelName);
            figure = unitdb::ModelNamed(key);
        }
        const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes: the model's fifth character
        if (fixes ? !pluginfixes::ModelIsFemale(figure->figureName) : figure->figureName == "unhuma")
            sex = "m";
        else if (fixes || figure->figureName == "unhufe")
            sex = "f";
        build = "me";
        model = str::Format("infa%s%s%dface", figure->figureName, build, prototype->hair + 1);
        picture = str::Format("face%s%02d", sex, prototype->skin);
        AddPortrait(model, picture);
        regions_[portraits_ - 1]->SetTipString(player->name_.data());
    }
}

// --- Update --------------------------------------------------------------------------------------

void CChatWindow::Update() {
    net::ClientSession* client = net::g_gameClient;
    if (Live(client->trade_)) {
        GameSendLeave04(9);
        return;
    }
    CUiSurface* surface = surfaces_[0];
    surface->Clear();
    std::string status, offer;
    int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? kFirstFaceLeft : kFirstStatusLeft;
    for (auto p = players_.begin(); p != players_.end(); ++p, left -= kFaceWidth) {
        net::PlayerMessage* player = PlayerOf(*p);
        status.clear();
        switch (player->state_) {
        case 0: status = texts_[0]; break;
        case 1: status = texts_[1]; break;
        case 2: status = texts_[2]; break;
        case 3:
        case 4: status = texts_[3]; break;
        default: break;
        }
        offer.clear();
        auto* own = client->party_.Get();
        if (own && own->partner_.ptr == *p) offer = texts_[kWeOffer];
        const auto& offers = client->offers_;
        for (auto& node : offers) {
            if (node.Get()->player_.ptr == *p) {
                offer = texts_[kTheyOffer];
                break;
            }
        }
        if (status.size() != 0) {
            RECT line{left, 0, left + kFaceWidth, 0xF};
            surface->DrawTextIn(status, &line, 0, kWhite, 1, 1);
        }
        if (offer.size() != 0) {
            RECT line{left, 0xF, left + kFaceWidth, 0x1E};
            surface->DrawTextIn(offer, &line, 0, kWhite, 1, 1);
        }
    }
}

// --- the input side's left button and mouse move -------------------------------------------------

void CChatWindow::LeftDown(int, int x, int y) {
    int index = RegionAt(x, y);
    if (index < 0 || static_cast<unsigned>(index) >= players_.size()) return;
    handled_ = true;
    // The active screen is the briefing, whose chat window this is (its chatActive_ is the pick mode).
    CBriefingScreen* briefing = interface_->ActiveWindowAs<CBriefingScreen>();
    int pick = briefing->chatActive_;
    if (pick != 0) {
        char text[0x80];
        std::snprintf(text, sizeof(text), "%d", PlayerOf(players_[index])->serial_);
        if (pick == 1)
            net::g_gameClient->SendCommand(kPickCommandA, text);
        else if (pick == 2)
            net::g_gameClient->SendCommand(kPickCommandB, text);
        briefing->chatActive_ = 0;
        return;
    }
    interface_->PlaySound(kOnOffSound);
    net::ClientSession* client = net::g_gameClient;
    CRefObject* player = players_[index];
    // The binding is not checked for null here.
    if (client->binding_->PlayerRef().ptr == player) return;
    auto* own = client->party_.Get();
    if (own && own->partner_.ptr == player) {
        net::ClientDropParty(client);
        return;
    }
    client->StartParty(player);
}

void CChatWindow::MouseMove(int, int x, int y) {
    int index = RegionAt(x, y);
    int pick = interface_->ActiveWindowAs<CBriefingScreen>()->chatActive_;
    if (pick != 0 && index >= 0 && static_cast<unsigned>(index) < players_.size()) g_cursor->SetShape(0);
}
