// A network client's player list on the base screen (CChatWindow). See
// ui_briefing.h and docs/notes/ui.md, section 3. Multiplayer only.
#include "game_data.h"
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

#include "lib/vecmath.h"

#include "ei/rebirth.h"

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
constexpr int kPickCommandA = 4, kPickCommandB = 5;   // the client's command kinds for picks 1 / 2

net::PlayerMessage* PlayerOf(CRefObject* object) { return static_cast<net::PlayerMessage*>(object); }
bool Live(const CRefObject* object) { return object && !(object->flags7 & CRefObject::kDetached); }

} // namespace

// --- the constructor -----------------------------------------------------------------------------

// Its own update; the input side's click and mouse move.
CChatWindow::CChatWindow(CInterface3D* ownerUi) : CUiWindow(ownerUi) {
    // EI_Plugin's NPlayers: the window and its surface start at x 0, not 0x1D0.
    const int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? 0 : 0x1D0;
    rect = RECT{left, 0, 800, 0xA0};
    CUiSurface* surface = new CUiSurface(ui, left, 0x4B, 800, 0x69, SurfaceBlend::Keyed, true);
    AddSurface(surface);
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

void CChatWindow::AddPortrait(const std::string& model, std::string_view texture) {
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

    CUiMesh* sprite = AddSprite(model);
    int left = kFirstFaceLeft - portraits * kFaceWidth;
    int right = left + kFaceWidth;
    if (sprite) {
        sprite->Place(static_cast<float>(left + right) * 0.5f, 32.0f, 7.0f);
        sprite->SetScale(0.3f);
        Vector3 axis{1.0f, 0.0f, 0.0f};
        Quaternion turn;
        // pi * 1.05, passed as a float.
        sprite->localRotation =
            *turn.SetAxisAngle(axis, 3.1415927f * 1.05f);
    } else {
        sprite = AddBlankSprite();
        sprite->SetRectangle(7.0f, 100.0f, static_cast<float>(left), 0.0f, static_cast<float>(right), 60.0f, 0, 0, 0x80,
                             0x80);
    }
    sprite->textureOffsetU = static_cast<float>(u) * 0.00390625f;
    sprite->textureOffsetV = static_cast<float>(v + 0x80) * 0.00390625f;
    sprite->texture = objects[std::ssize(objects) - 1];
    ++portraits;
    AddRegion()->SetRect(left, 0, right, 0x50);
}

// --- the players' faces --------------------------------------------------------------------------

void CChatWindow::ShowPlayers() {
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
        CRefObject* player = node->Get();
        bool own = client->binding->PlayerRef().Get() == player;
        if (own)
            players.emplace(players.begin(), player);
        else
            players.emplace_back(player);
        ++listed;
    }

    // The faces (the strings live across players: the sex letter carries over).
    std::string model, picture, sex, build;
    for (auto& p : players) {
        net::PlayerMessage* player = PlayerOf(p);
        UnitPrototypeRecord* prototype;
        {
            prototype = unitdb::PrototypeNamed(std::string(player->text));
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
        model = std::format("infa{}{}{}face", figure->figureName, build, prototype->hair + 1);
        picture = std::format("face{}{:02}", sex, prototype->skin);
        AddPortrait(model, picture);
        regions[portraits - 1]->SetTipString(player->name.data());
    }
}

// --- Update --------------------------------------------------------------------------------------

void CChatWindow::Update() {
    net::ClientSession* client = net::g_gameClient;
    if (Live(client->trade)) {
        GameSendLeave04(9);
        return;
    }
    CUiSurface* surface = surfaces[0];
    surface->Clear();
    std::string status, offer;
    int left = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? kFirstFaceLeft : kFirstStatusLeft;
    for (auto p = players.begin(); p != players.end(); ++p, left -= kFaceWidth) {
        net::PlayerMessage* player = PlayerOf(*p);
        status.clear();
        switch (player->state) {
        case 0: status = texts[0]; break;
        case 1: status = texts[1]; break;
        case 2: status = texts[2]; break;
        case 3:
        case 4: status = texts[3]; break;
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
    if (index < 0 || static_cast<unsigned>(index) >= players.size()) return;
    handled = true;
    // The active screen is the briefing, whose chat window this is (its chatActive is the pick mode).
    CBriefingScreen* briefing = ui->ActiveWindowAs<CBriefingScreen>();
    int pick = briefing->chatActive;
    if (pick != 0) {
        char text[0x80];
        std::snprintf(text, sizeof(text), "%d", PlayerOf(players[index])->serial);
        if (pick == 1)
            net::g_gameClient->SendCommand(kPickCommandA, text);
        else if (pick == 2)
            net::g_gameClient->SendCommand(kPickCommandB, text);
        briefing->chatActive = 0;
        return;
    }
    ui->PlaySound(kOnOffSound);
    net::ClientSession* client = net::g_gameClient;
    CRefObject* player = players[index];
    // The binding is not checked for null here.
    if (client->binding->PlayerRef().Get() == player) return;
    auto* own = client->party.Get();
    if (own && own->partner.Get() == player) {
        net::ClientDropParty(client);
        return;
    }
    client->StartParty(player);
}

void CChatWindow::MouseMove(int, int x, int y) {
    int index = RegionAt(x, y);
    int pick = ui->ActiveWindowAs<CBriefingScreen>()->chatActive;
    if (pick != 0 && index >= 0 && static_cast<unsigned>(index) < players.size()) g_cursor.SetShape(0);
}
