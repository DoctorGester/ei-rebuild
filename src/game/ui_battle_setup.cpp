// The battle screen's Create: the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "core_streams.h"
#include "ui.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "party.h"
#include "world.h"
#include "model.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_string.h"
#include "camera.h"

using console::CStringArray;

namespace {
// The game's save stream's version and a game being loaded (the save stream holds its data).
int SaveVersion() { return static_cast<int>(GameSaveStream()->version_); }
bool LoadingGame() { return GameSaveStream()->size_ != 0; }

} // namespace

// The panels Create builds inline (they have no constructors of their own).
CHudUnitPanel::CHudUnitPanel(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    texts_.Construct();
    partLabels_.Construct();
}

CHudMap::CHudMap(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    for (UiPointArray* points : {&viewPolygon_, &visibleArea_}) {
        points->data = nullptr;
        points->growBy = 0;
        points->capacity = 0;
        points->size = 0;
    }
}

// --- Create ---------------------------------------------------------------------------

void CBattleScreen::CreateScreen() {
    float mapZoom = 1.0f;
    portraitMode_ = 0;
    std::string savedText;
    hasHero_ = false;
    pendingExit_ = -1;
    exitPending_ = 0;

    if (LoadingGame()) {
        // The screen's part of the saved game.
        CMemoryStream* stream = GameSaveStream();
        stream->Seek(0);
        int count;
        stream->Read(&count, 4);
        heroes_.SetSize(count, -1);
        for (int i = 0; i < heroes_.size; ++i) stream->Read(&heroes_.data[i], 4);
        stream->Read(&mapZoom, 4);
        std::uint8_t serverFlag;
        stream->Read(&serverFlag, 1);
        GameSetServerPaused(serverFlag);
        if (SaveVersion() >= 0x47) {
            int unused;
            stream->Read(&unused, 4);
            alignas(4) std::uint8_t raw[sizeof(mfc::CString)];
            mfc::CString* text = stream->ReadString(raw);
            savedText = *text;
            std::destroy_at(text);
        }
        if (SaveVersion() >= 0x62) {
            stream->Read(&exitPending_, 1);
            stream->Read(&pendingExit_, 4);
        }
        stream->Resize(0);
    } else {
        // A new game: the local player's first hero.
        if (LocalPlayerData()->members_.size > 0) {
            CParty* player = LocalPlayerData();
            int index = heroes_.size;
            std::uint32_t hero = player->members_.data[0].unitId;
            heroes_.SetSize(index + 1, -1);
            heroes_.data[index] = hero;
            hasHero_ = true;
        }
    }

    // The screen's own input handler (0x0C bytes).
    UiInputSide* input = new UiInertInput(interface_);
    AddInput(input);

    CreateTexture(0x100, 0x100, 0x20);
    CreateTexture(0x100, 0x100, 0x20);
    LoadTexture("battle00", 3, 0);

    // Panels built by their own constructors (Rebirth-patched: reproduced behind its flags, rule 9).
    weaponBar_ = new CHudWeaponBar(interface_, this);
    AddChild(weaponBar_);
    weaponBar_->AddObject(objects_.data[0]);

    quickItems_ = new CHudQuickItems(interface_, this);
    AddChild(quickItems_);
    quickItems_->AddObject(objects_.data[0]);

    spellColumn_ = new CHudSpellColumn(interface_, this);
    AddChild(spellColumn_);
    spellColumn_->AddObject(objects_.data[1]);

    actionColumn_ = new CHudActionColumn(interface_, this);
    AddChild(actionColumn_);
    actionColumn_->AddObject(objects_.data[1]);

    portraits_ = new CHudPortraits(interface_, this);
    AddChild(portraits_);
    portraits_->AddObject(objects_.data[2]);

    unitPanel_ = new CHudUnitPanel(interface_, this);
    AddChild(unitPanel_);
    unitPanel_->AddObject(objects_.data[2]);
    unitPanel_->AddObject(objects_.data[1]);
    unitPanel_->SetupVanilla(interface_->hudBodyPart_, interface_->hudPanelsIn_[0]);

    map_ = new CHudMap(interface_, this);
    AddChild(map_);
    map_->AddObject(objects_.data[2]);
    map_->SetupVanilla(mapZoom, interface_->hudPanelsIn_[2]);

    clock_ = new CHudClock(interface_, this);
    AddChild(clock_);
    clock_->AddObject(objects_.data[2]);
    clock_->SetupVanilla(interface_->fastGame_);

    movePanel_ = new CHudMovePanel(interface_, this);
    AddChild(movePanel_);
    movePanel_->AddObject(objects_.data[2]);
    movePanel_->Build();

    messageLog_ = new CUiScrollText(interface_, game::EmptyText(), RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f);
    AddChild(messageLog_);
    messageLog_->align_ = 1;

    topPanel_ = new CHudTopPanel(interface_, this);
    AddChild(topPanel_);
    topPanel_->AddObject(objects_.data[2]);
    topPanel_->Build(interface_->hudTopPage_, interface_->hudPanelsIn_[1]);
    {
        // The saved message text goes back to the top panel and, when it shows page 0, the log.
        auto* top = topPanel_;
        mfc::CString& panelText = top->pageTexts_[0];
        const char* text = savedText.c_str();
        panelText += text;
        if (top->page_ == 0) {
            // The owner is this screen (a container; CBattleScreen declares it as a window).
            auto* owner = top->owner_;
            owner->MessageLog()->AddText(text);
        }
    }
    topPanel_->ShowItems();

    AddInput(new CCameraInput(interface_));

    TheCamera()->flags_ &= ~0x40u;
    pendingAction_ = 0;
    pendingSlot_ = -1;
    GameSetWeather(0, 0.0f);   // clear weather
    GameStartAmbience();
    party_.SetSize(0, -1);
    viewPressed_ = false;
    boxSelecting_ = false;
    std::memset(orderKeys_, 0, 6);   // +0x18D dword and +0x191 word
    questSound_ = 0;

    // The "pers <name>" texts: one line per person, by the person's text key.
    LoadPersonTexts(&personTexts_);
    spare_ = 0;
    if (IsMultiplayerClient()) {
        PlayersChanged(1);
    }
    clientParty_.camera = -1;
}

// --- Drawing ---------------------------------------------------------------

namespace {

constexpr DWORD kClearTarget = 2;         // D3DCLEAR_ZBUFFER
constexpr DWORD kClearStencil = 6;        // D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL

} // namespace

BOOL CBattleScreen::Draw(int pass) {
    GameRender();
    if (boxSelecting_) {   // the selection box, a dark rectangle with an outline
        reinterpret_cast<CSceneRenderer*>(pass)->DrawRectangle(selectionBox_.left, selectionBox_.top, selectionBox_.right,
                                                            selectionBox_.bottom, 0x40, 1);
    }
    Input()->MouseMove(0, MouseScreenX(), MouseScreenY());
    // The depth buffer (and stencil, when the device has one) is cleared before the panels draw.
    UiRenderer* renderer = interface_->renderer_;
    DWORD flags = kClearTarget;
    if ((renderer->caps_ & 0x4000) && renderer->stencil_) flags = kClearStencil;
    auto* lock = TheDeviceLock();
    gameapi::EnterCriticalSection(lock);
    renderer->result_ = renderer->device_->Clear(0, nullptr, flags, 0, 1.0f, 0);
    gameapi::LeaveCriticalSection(lock);
    render::ClearDepth(1.0f, nullptr, 0);
    // Rebirth's AddNewConsoleCommands (call redirected): "nogui" shows or hides the
    // panels.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) console::ApplyAddonGuiVisibility(this);
    CUiContainer::Draw(pass);
    return 1;
}

EI_REWRITTEN(0x00635250, CBattleScreen::CreateScreen);
EI_REWRITTEN(0x00636020, CBattleScreen::Draw);
