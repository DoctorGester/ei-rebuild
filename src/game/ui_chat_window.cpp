// A network client's player list on the base screen (ChatWindow). See
// ui_briefing.h and docs/notes/ui.md, section 3. Multiplayer only.
#include "game_singletons.h"
#include "text_manager.h"
#include "net_classes.h"
#include "net_messages.h"
#include "plugin_fixes.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "unit_db.h"
#include "world_glue.h"
#include "world.h"

#include "lib/vecmath.h"

#include "lib/rebirth.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <format>

namespace {

// The status texts' keys, in texts order.
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

constexpr int kFaceWidth = 0x38;
constexpr int kFirstFaceLeft = 0x2E8;
constexpr int kFirstStatusLeft = 0x118;   // with the window at 0x1D0; 0x2E8 with EI_Plugin's NPlayers
constexpr std::uint32_t kWhite = 0xFFFFFF;

bool Live(const RefObject* object) { return object && !(object->flags7 & RefObject::kDetached); }

} // namespace

// --- the constructor -----------------------------------------------------------------------------

// Its own update; the input side's click and mouse move.
ChatWindow::ChatWindow(Interface3D* ownerUi, BriefingScreen* screen) : UiWindow(ownerUi), briefing(screen) {
    // EI_Plugin's NPlayers: the window and its surface start at x 0, not 0x1D0.
    const int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? 0 : 0x1D0;
    rect = Rect{left, 0, 800, 0xA0};
    UiSurface* surface = new UiSurface(ui, left, 0x4B, 800, 0x69, SurfaceBlend::Keyed, true);
    AddSurface(surface);
    surface->PaintWith([this](UiSurface& out) { PaintStatus(out); });
    portraits = 0;
    std::string text;
    for (const char* key : kTextKeys) {
        text = g_clientWorld.texts->GetString(key);
        texts.push_back(text);
    }
}
// EI_Plugin.asi patches the constructor and Update (NPlayers: the window's left edge and the first status
// column) and ShowPlayers (Fixes: the faces' sex and the prototype fallback) in a Rebirth launch.

// --- a face --------------------------------------------------------------------------------------

void ChatWindow::AddPortrait(const std::string& model, std::string_view texture) {
    // Four faces share a 256 x 256 texture, one quarter each.
    int u = 0, v = 0;
    switch (portraits % 4) {
    case 0: CreateTexture(0x100, 0x100, TextureKind::Blank1555); break;
    case 1: u = 0x80; break;
    case 2: v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: break;   // a negative count: u is unset and `texture` is used as v
    }
    CopyImage(objects[std::ssize(objects) - 1], texture, 0x80, 0x80, 0x80, 0x80, u, v);

    UiMesh* sprite = AddSprite(model);
    int left = kFirstFaceLeft - portraits * kFaceWidth;
    int right = left + kFaceWidth;
    if (sprite) {
        sprite->Place((left + right) * 0.5f, 32.0f, 7.0f);
        sprite->SetScale(0.3f);
        Vector3 axis{1.0f, 0.0f, 0.0f};
        // pi * 1.05, passed as a float.
        sprite->localRotation =
            Quaternion::AxisAngle(axis, 3.1415927f * 1.05f);
    } else {
        sprite = AddBlankSprite();
        sprite->SetRectangle(7.0f, kSpriteScale, static_cast<float>(left), 0.0f, static_cast<float>(right), 60.0f, 0, 0, 0x80,
                             0x80);
    }
    sprite->textureOffsetU = u * 0.00390625f;
    sprite->textureOffsetV = (v + 0x80) * 0.00390625f;
    sprite->texture = objects[std::ssize(objects) - 1];
    ++portraits;
    AddRegion()->SetRect(left, 0, right, 0x50);
}

// --- the players' faces --------------------------------------------------------------------------

void ChatWindow::ShowPlayers() {
    ClearSceneObjects();
    ClearRegions();
    ClearObjects();
    portraits = 0;
    listed = 0;

    players.clear();

    // Our own player first, the others after it; none while the binding is gone.
    net::ClientSession* client = net::g_gameClient;
    const auto& clientPlayers = client->players;
    for (auto node = clientPlayers.begin(); node != clientPlayers.end(); ++node) {
        if (!Live(client->binding)) continue;
        net::PlayerMessage* player = node->Get();
        bool own = client->binding->player.Get() == player;
        if (own)
            players.emplace(players.begin(), player);
        else
            players.emplace_back(player);
        ++listed;
    }

    // The faces (the strings live across players: the sex letter carries over).
    std::string model, picture, sex, build;
    for (auto& p : players) {
        net::PlayerMessage* player = p;
        UnitPrototypeRecord* prototype;
        {
            prototype = g_unitDatabase.prototypes.FindByName(std::string(player->text));
            // EI_Plugin's Fixes: an unknown prototype becomes the first one that is no creature.
            if (!prototype && pluginfixes::Enabled())
                prototype = pluginfixes::FallbackPrototype();
        }
        UnitModelRecord* figure;
        {
            std::string key(prototype->modelName);
            figure = g_unitDatabase.models.FindByName(key);
        }
        const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes: the model's fifth character
        if (fixes ? !pluginfixes::ModelIsFemale(figure->figureName) : figure->figureName == "unhuma")
            sex = "m";
        else if (fixes || figure->figureName == "unhufe")
            sex = "f";
        build = "me";
        model = std::format("infa{}{}{}face", figure->figureName, build, prototype->hair + 1);
        picture = std::format("face{}{:02}", sex, prototype->skin);
        AddPortrait(model, picture);
        regions[portraits - 1]->SetTipString(player->name.data());
    }
}

// --- Update --------------------------------------------------------------------------------------

void ChatWindow::Update() {
    net::ClientSession* client = net::g_gameClient;
    if (Live(client->trade)) {
        GameRequestGameState(9);
        return;
    }
    statusRows.clear();
    std::string status, offer;
    int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? kFirstFaceLeft : kFirstStatusLeft;
    for (auto p = players.begin(); p != players.end(); ++p, left -= kFaceWidth) {
        net::PlayerMessage* player = *p;
        status.clear();
        switch (player->state) {
        case net::PlayerStatus::InZone: status = texts[0]; break;
        case net::PlayerStatus::InBase: status = texts[1]; break;
        case net::PlayerStatus::InCamp: status = texts[2]; break;
        case net::PlayerStatus::Connecting:
        case net::PlayerStatus::Fresh: status = texts[3]; break;
        default: break;
        }
        offer.clear();
        auto* own = client->party.Get();
        if (own && own->partner.Get() == *p) offer = texts[kWeOffer];
        const auto& offers = client->offers;
        for (auto& node : offers) {
            if (node.Get()->player.Get() == *p) {
                offer = texts[kTheyOffer];
                break;
            }
        }
        statusRows.push_back({left, status, offer});
    }
}

void ChatWindow::PaintStatus(UiSurface& surface) const {
    for (const StatusRow& row : statusRows) {
        if (row.status.size() != 0) {
            Rect line{row.left, 0, row.left + kFaceWidth, 0xF};
            surface.DrawTextIn(row.status, line, TextStyle{0, kWhite, render::text::kCenter});
        }
        if (row.offer.size() != 0) {
            Rect line{row.left, 0xF, row.left + kFaceWidth, 0x1E};
            surface.DrawTextIn(row.offer, line, TextStyle{0, kWhite, render::text::kCenter});
        }
    }
}

// --- the input side's left button and mouse move -------------------------------------------------

void ChatWindow::LeftDown(Point point) {
    int index = RegionAt(point);
    if (index < 0 || static_cast<unsigned>(index) >= players.size()) return;
    handled = true;
    // The game reads the active screen: the briefing, whose child this is, while the chat gets input.
    int pick = briefing->chatActive;
    if (pick != 0) {
        char text[0x80];
        std::snprintf(text, sizeof(text), "%d", players[index]->serial);
        if (pick == 1)
            net::g_gameClient->SendCommand(net::CommandKind::KickBySerial, text);
        else if (pick == 2)
            net::g_gameClient->SendCommand(net::CommandKind::BanBySerial, text);
        briefing->chatActive = 0;
        return;
    }
    ui->PlayInterfaceSound(kOnOffSound);
    net::ClientSession* client = net::g_gameClient;
    net::PlayerMessage* player = players[index];
    // The binding is not checked for null here.
    if (client->binding->player.Get() == player) return;
    auto* own = client->party.Get();
    if (own && own->partner.Get() == player) {
        net::ClientDropParty(client);
        return;
    }
    client->StartParty(player);
}

void ChatWindow::MouseMove(Point point) {
    int index = RegionAt(point);
    int pick = briefing->chatActive;
    if (pick != 0 && index >= 0 && static_cast<unsigned>(index) < players.size()) g_cursor.SetShape(CursorShape::Default);
}
