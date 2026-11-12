// Ours: the companions window (ui_companions.h). See docs/notes/ui.md ("Companions window").
#include "game/ui/ui_companions.h"

#include "game/app/singletons.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "game/net/net_session.h"
#include "game/players/saved_parties.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_hud.h"
#include "game/ui/ui_hud_pieces.h"
#include "game/ui/ui_menu_pieces.h"
#include "lib/containers.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_message_box.h"
#include "game/world/world.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace {

constexpr int kTop = 150;
constexpr int kRowTop = 200;
constexpr int kRowHeight = 24;
constexpr int kMostRows = 8;
constexpr int kBottom = kRowTop + kMostRows * kRowHeight + 20;
constexpr std::uint32_t kTitleColour = 0x31B3FF;
constexpr std::uint32_t kPickedColour = 0x31E3EE;
constexpr std::uint32_t kButtonFrameColour = 0x4B646E;   // the portraits' frame's bronze
// What the window leaves pending for the battle screen, which takes no answer from it.
constexpr std::uint32_t kClosed = BoxResult(MessageQuestion::Fifth, MessageAnswer::Cancel);

struct AddedText {
    const char* russian = nullptr;
    const char* english = nullptr;
};
constexpr AddedText kTitle = {"Спутники", "Companions"};
constexpr AddedText kNoHeroes = {"Других героев нет", "No other heroes"};
constexpr AddedText kNoRoom = {"Больше спутников не взять", "No room for more companions"};
constexpr AddedText kWithYou = {"(с вами)", "(with you)"};
constexpr AddedText kAdd = {"OK: взять", "OK: add"};
constexpr AddedText kRemove = {"OK: отпустить", "OK: send away"};
constexpr AddedText kButtonTip = {"Спутники (F3)", "Companions (F3)"};
const char* Shown(const AddedText& text) { return GameTextsInRussian() ? text.russian : text.english; }

// A hero's name as the hero list shows it: its script name up to the '|'.
std::string HeroName(const MpHeroRecord& hero) {
    const std::string& name = hero.party.members[0].scriptName;
    return name.substr(0, name.find('|'));
}

bool Room() { return std::ssize(g_savedParties.companions) < net::BaseCompanionLimit(); }

} // namespace

bool CompanionsOffered() { return IsMultiplayerClient() && net::BaseCompanionLimit() > 0; }

void CompanionWindow::CreateScreen() {
    LoadTexture("saveload", TextureKind::Interface);
    for (const std::string& file : g_savedParties.companions) {
        const MpHeroRecord* hero = g_savedParties.Entry(file);
        if (!hero || hero->party.members.size() != 1) continue;
        files.push_back(file);
        names.push_back(HeroName(*hero) + " " + Shown(kWithYou));
    }
    companionRows = lib::Count(files);
    for (MpHeroRecord& hero : g_savedParties.entries) {
        if (g_savedParties.InUse(&hero) || hero.party.members.size() != 1) continue;
        if (std::ssize(files) == kMostRows) break;
        files.push_back(hero.file);
        names.push_back(HeroName(hero));
    }
    picked = files.empty() ? -1 : 0;
    AddSurface(NewSurface(Rect{200, kTop, 600, kBottom}))->PaintWith([this](UiSurface& out) { Paint(out); });
    Shade(this, 3.0f, 200.0f, static_cast<float>(kTop), 600.0f, static_cast<float>(kBottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(kTop - 5), 605.0f, static_cast<float>(kBottom + 5));
    // OK (texels 0x51..0x9B) is Add, cancel (0xA0..0xEA) closes.
    const float buttonTop = static_cast<float>(kBottom + 0x1A);
    const float buttonBottom = static_cast<float>(kBottom + 0x4A);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
    addButton = AddRect(this, 263, kBottom + 0x1A, 337, kBottom + 0x4A);
    closeButton = AddRect(this, 463, kBottom + 0x1A, 537, kBottom + 0x4A);
    for (int i = 0; i < std::ssize(files); ++i)
        rows.push_back(AddRect(this, 215, kRowTop + i * kRowHeight, 585, kRowTop + (i + 1) * kRowHeight));
}

void CompanionWindow::Paint(UiSurface& out) const {
    PaintLabels(out, {{Shown(kTitle), Rect{15, 10, 385, 40}, {2, kTitleColour, render::text::Format::Center}}});
    const int top = kRowTop - kTop;
    for (int i = 0; i < std::ssize(names); ++i) {
        const Rect row{15, top + i * kRowHeight, 385, top + (i + 1) * kRowHeight};
        const bool chosen = i == picked;
        PaintLabels(out, {{names[i], row, {2, chosen ? kPickedColour : kUiTextColour, render::text::Format::Left}}});
        if (chosen) out.FrameRect(Rect{row.left - 5, row.top, row.right, row.bottom}, kPickedColour);
    }
    // Below the rows: what OK does, or why it does nothing.
    const char* note = picked < 0 ? Shown(kNoHeroes) : picked < companionRows ? Shown(kRemove)
                       : Room()                         ? Shown(kAdd)
                                                        : Shown(kNoRoom);
    const int noteTop = top + kMostRows * kRowHeight - 4;
    if (note)
        PaintLabels(out, {{note, Rect{15, noteTop, 385, noteTop + kRowHeight}, {2, kUiTextColour, render::text::Format::Center}}});
}

void CompanionWindow::Act() {
    MpHeroRecord* hero = picked >= 0 && picked < std::ssize(files) ? g_savedParties.Entry(files[picked]) : nullptr;
    PlayerClient* client = g_clientWorld.LocalView();
    if (hero && picked < companionRows) {
        // Sent away: its record (the camp's edits) goes back to its file first, then its unit leaves the party.
        const int unitId = g_savedParties.ReturnCompanion(*LocalPlayerData(), hero->file);
        std::erase(g_savedParties.companions, hero->file);
        if (unitId >= 0) client->SendRemoveCompanion(unitId);
    } else if (hero && Room()) {
        g_savedParties.companions.push_back(hero->file);
        client->SendAddCompanion(hero->GetParty(), false);
    }
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    g_interface.SetPendingValue(kClosed);
}

void CompanionWindow::Close() {
    g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
    g_interface.SetPendingValue(kClosed);
}

void CompanionWindow::OnEvent(UiEvent& event) {
    if (event.kind == UiEventKind::LeftDown) {
        UiRegion* region = RegionUnder(event.at);
        if (region == addButton) {
            Act();
        } else if (region == closeButton) {
            Close();
        } else {
            for (int i = 0; i < std::ssize(rows); ++i)
                if (region == rows[i]) picked = i;
        }
        event.handled = true;
        return;
    }
    if (event.kind == UiEventKind::KeyDown && !event.repeat) {
        if (event.key == platform::Key::Return) {
            Act();
            event.handled = true;
            return;
        }
        if (event.key == platform::Key::Escape) {
            Close();
            event.handled = true;
            return;
        }
    }
    UiContainer::OnEvent(event);
}

void CompanionWindow::CloseScreen() {
    ClearSceneObjects();
    ClearSurfaces();
}

void CompanionButton::Update() {
    if (built) return;
    built = true;
    const int left = 762, top = 562, right = left + 30, bottom = top + 30;   // the screen's bottom right
    rect = Rect{left, top, right, bottom};
    Shade(this, 7.0f, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom));
    AddSurface(NewSurface(rect))->PaintWith([](UiSurface& out) {
        out.FrameRect(Rect{0, 0, 30, 30}, kButtonFrameColour);
        out.FrameRect(Rect{1, 1, 29, 29}, kButtonFrameColour);
        PaintLabels(out, {{"+", Rect{0, 2, 30, 30}, {2, kTitleColour, render::text::Format::Center}}});
    });
    button = AddRect(this, left, top, right, bottom);
    button->SetTipString(Shown(kButtonTip));
}

void CompanionButton::OnEvent(UiEvent& event) {
    if (!button) return;
    DispatchMouse(event, [&] { PressRegion(event); },
                  [&] {
                      if (PressedRegion(event) != button) return;
                      g_interface.PlayInterfaceSound(kMessageBoxOkSound);
                      g_interface.PushWindow(std::make_unique<CompanionWindow>(), WindowBackdrop::FirstWindow);
                  },
                  [&] { HitRegion(event); });
}
