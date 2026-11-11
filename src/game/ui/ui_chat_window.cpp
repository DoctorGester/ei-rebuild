// A network client's player list on the base screen (ChatWindow). See
// ui_briefing.h and docs/notes/ui.md, section 3. Multiplayer only.
#include "game/app/singletons.h"
#include "engine/data/text_manager.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/faces.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_briefing.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit_db.h"
#include "game/players/player_client.h"
#include "game/world/world.h"

#include "lib/vecmath.h"

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
constexpr int kFirstFaceLeft = 0x2E8;     // the faces go leftwards from here, kFaceWidth apart
constexpr int kOriginalLeft = 0x1D0;     // the window's left edge for up to 6 players (the original's 6 faces)
constexpr int kOriginalPlayers = 6;
constexpr std::uint32_t kWhite = 0xFFFFFF;

} // namespace

// --- the constructor -----------------------------------------------------------------------------

// Its own update; its click and mouse move.
ChatWindow::~ChatWindow() = default;

ChatWindow::ChatWindow(BriefingScreen* screen) : briefing(screen) {
    // EI_Plugin's NPlayers: for a game of more than 6 players (ClientSession::PlayerLimit) the window and its surface
    // start at x 0, not 0x1D0.
    const int left = net::g_gameClient->PlayerLimit() > kOriginalPlayers ? 0 : kOriginalLeft;
    rect = Rect{left, 0, 800, 0xA0};
    UiSurface* surface = AddSurface(std::make_unique<UiSurface>(left, 0x4B, 800, 0x69, SurfaceBlend::Keyed, true));
    surface->PaintWith([this](UiSurface& out) { PaintStatus(out); });
    std::string text;
    for (const char* key : kTextKeys) {
        text = g_clientWorld.texts->GetString(key);
        texts.push_back(text);
    }
}
// EI_Plugin's NPlayers moves the window's left edge (the constructor) and with it the first status column (Update);
// its Fixes patch ShowPlayers (the faces' sex and the prototype fallback).

// --- a face --------------------------------------------------------------------------------------

UiRegion* ChatWindow::AddPortrait(const std::string& model, std::string_view texture) {
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
    UiRegion* region = AddRegion();
    region->SetRect(left, 0, right, 0x50);
    return region;
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
        if (!IsLive(client->binding)) continue;
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
            // EI_Plugin's Fixes (always on): an unknown prototype becomes the first one that is no creature.
            if (!prototype) prototype = pluginfixes::FallbackPrototype();
        }
        UnitModelRecord* figure;
        {
            std::string key(prototype->modelName);
            figure = g_unitDatabase.models.FindByName(key);
        }
        // EI_Plugin's Fixes (always on): the model's fifth character.
        sex = pluginfixes::ModelIsFemale(figure->figureName) ? "f" : "m";
        build = "me";
        model = std::format("infa{}{}{}face", figure->figureName, build, prototype->hair + 1);
        picture = std::format("face{}{:02}", sex, prototype->skin);
        AddPortrait(model, picture)->SetTipString(player->name.data());
    }
}

// --- Update --------------------------------------------------------------------------------------

void ChatWindow::Update() {
    net::ClientSession* client = net::g_gameClient;
    if (IsLive(client->trade)) {
        g_interface.closeAll = 1;
        g_clientWorld.LocalView()->RequestGameState(9);
        return;
    }
    statusRows.clear();
    std::string status, offer;
    int left = kFirstFaceLeft - rect.left;   // the surface's x of the first face
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
            surface.DrawTextIn(row.status, line, TextStyle{0, kWhite, render::text::Format::Center});
        }
        if (row.offer.size() != 0) {
            Rect line{row.left, 0xF, row.left + kFaceWidth, 0x1E};
            surface.DrawTextIn(row.offer, line, TextStyle{0, kWhite, render::text::Format::Center});
        }
    }
}

// --- the left button and mouse move --------------------------------------------------------------

void ChatWindow::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    default: break;
    }
}

void ChatWindow::LeftDown(UiEvent& event) {
    const int index = RegionIndex(regions, RegionUnder(event.at));
    if (index < 0 || static_cast<unsigned>(index) >= players.size()) return;
    event.handled = true;
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
    g_interface.PlayInterfaceSound(kOnOffSound);
    net::ClientSession* client = net::g_gameClient;
    net::PlayerMessage* player = players[index];
    // The binding is not checked for null here.
    if (client->binding->player.Get() == player) return;
    auto* own = client->party.Get();
    if (own && own->partner.Get() == player) {
        client->DropParty();
        return;
    }
    client->StartParty(player);
}

void ChatWindow::MouseMove(UiEvent& event) {
    const int index = RegionIndex(regions, RegionUnder(event.at));
    int pick = briefing->chatActive;
    if (pick != 0 && index >= 0 && static_cast<unsigned>(index) < players.size()) g_cursor.SetShape(CursorShape::Default);
}
