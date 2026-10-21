// The briefing / base screen's main-vtable slots (CBriefingScreen, ui_briefing.h): close, message
// box answers, a unit leaving the party, game over, the "briefing" command, the chat window and the
// saved state. See docs/notes/ui.md, "Briefing / base screen".
#include "ui.h"
#include "ui_briefing.h"
#include "game_strings.h"

#include "camera.h"
#include "core_streams.h"
#include "object_tables.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>
#include <new>

namespace {

// The map edge fade (a float the zone screens set while they are open).
float& MapEdgeFade() { return ei::GameData<float>(0x0079BD34); }

// The load window the game-over box opens (0x194 bytes, built inline).
void** SaveLoadVtable() { return ei::GamePointer<void*>(0x00741388); }
void** SaveLoadInputVtable() { return ei::GamePointer<void*>(0x00741354); }

constexpr std::uint32_t kLeaveZoneYes = 0x10001;
constexpr std::uint32_t kGameOverLoad = 0x20001;
constexpr std::uint32_t kGameOverQuit = 0x20002;
constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;
constexpr int kGameOverResult = 0x20000;

// 006091C0: the chat window shows itself (thiscall, plain RET).
__declspec(naked) void __fastcall ChatWindowShowAt(CUiWindow* /*chat*/) EI_ORIGINAL(0x006091C0)

// The local player's unit records as the party order follows them (LocalPlayerData).
struct LocalPartyRecords {
    std::uint8_t unused_00[0x30];
    PartyUnitRecord* records;        // +0x30 0x784 bytes each, the unit's id at +0x1C
    int count;                       // +0x34
};
LocalPartyRecords* LocalRecords() { return reinterpret_cast<LocalPartyRecords*>(LocalPlayerData()); }
int RecordUnitId(const PartyUnitRecord& record) { return static_cast<int>(record.unitId); }

const char* ZoneTopicFormat() { return "b.%s"; }
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;

} // namespace

// --- The variable copies ------------------------------------------------------------------------

BriefingVariables::BriefingVariables(const char* prefix) {
    table_.ConstructDefault();   // 00581790
    PlayerZeroVariables(prefix, &table_);
}

BriefingVariables::~BriefingVariables() { table_.Destroy(); }

NamedFloatTable::Node* BriefingVariables::First() const { return table_.First(); }

NamedFloatTable::Node* BriefingVariables::Next(NamedFloatTable::Node* node) const { return table_.Next(node); }

// --- 00602BB0: slot 26 --------------------------------------------------------------------------

void CBriefingScreen::Close() {
    MapEdgeFade() = savedEdgeFade_;
    GameStopSounds(1);
    GameStopAmbience();
    std::uint32_t& flags = TheCamera()->flags_;
    flags = (flags & ~(CCamera::kBounded | CCamera::kAlternate)) | CCamera::kFree;
}
EI_REPLACE_METHOD(0x00602BB0, CBriefingScreen::Close);

// --- 00606F40: slot 27 --------------------------------------------------------------------------

void CBriefingScreen::OnMessageResult(std::uint32_t result) {
    switch (result) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(exit_);
        break;
    case kGameOverLoad: {
        auto* window = static_cast<CSaveLoadWindow*>(gamecrt::operator_new(sizeof(CSaveLoadWindow)));
        if (window) {
            static_cast<CUiContainer*>(window)->Construct(interface_);
            window->entriesBegin_ = nullptr;
            window->entriesEnd_ = nullptr;
            window->entriesCapacity_ = nullptr;
            window->isSave_ = false;
            window->sliding_ = false;
            new (static_cast<void*>(&window->name_)) mfc::CString();
            window->vtable_ = SaveLoadVtable();
            window->inputVtable_ = SaveLoadInputVtable();
        }
        // (Like the original, the flags are written again without checking the allocation.)
        window->isSave_ = false;
        window->sliding_ = false;
        interface_->PushWindow(window, 1, 1);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        interface_->closeAll_ = true;
        TheGameFlow()->DropSessions();
        uiorig::LeaveGame();
        break;
    default:
        break;
    }
    if (IsMultiplayerClient()) VirtualShowChat(1);
}

// --- 00602C90: slot 29 --------------------------------------------------------------------------

void CBriefingScreen::OnUnitJoined(CUnit* unit) {
    LocalPartyRecords* player = LocalRecords();
    int count = player->count;
    if (count == 0) return;
    int place = 0;
    int record = 0;
    for (; record < count; ++record) {
        int id = RecordUnitId(player->records[record]);
        if (unit->id_ == id) break;
        if (place < party_.size && reinterpret_cast<CUnit*>(party_.data[place])->id_ == id) ++place;
    }
    if (record < count) {
        // CArray::InsertAt(place, unit) inlined (the slot is zeroed, 0060AEC0, then written); the
        // original then looks the unit up in the server world without using the result.
        int size = party_.size;
        if (place >= size) {
            party_.SetSize(place + 1, -1);
        } else {
            party_.SetSize(size + 1, -1);
            std::memmove(&party_.data[place + 1], &party_.data[place], (size - place) * sizeof(party_.data[0]));
        }
        party_.data[place] = reinterpret_cast<std::uintptr_t>(unit);
    } else {
        // 0060AD50: SetAtGrow(size, unit).
        int size = party_.size;
        party_.SetSize(size + 1, -1);
        party_.data[size] = reinterpret_cast<std::uintptr_t>(unit);
    }
    if (unit->id_ != RecordUnitId(player->records[0])) return;

    leader_ = unit;
    TheClientWorld()->Select(unit);
    TheCamera()->SetTarget(unit);
    MapRecord* zone;
    {
        BriefingPlayerName name;
        zone = TheMapDatabase()->FindMap(name);
    }
    mfc::CString prefix;
    prefix.Format(ZoneTopicFormat(), zone->name);
    BriefingVariables variables(prefix);
    console::CStringArray parts;
    for (NamedFloatTable::Node* node = variables.First(); node; node = variables.Next(node)) {
        std::uint32_t bits;
        std::memcpy(&bits, &node->value.mapped, 4);
        if (bits != kOneBits) continue;
        text::Split(node->value.key, &parts, '.');
        if (parts.size != kTopicNameParts) continue;
        topic_ = parts[2];
        briefingStarted_ = true;
        StartDialogue(1, 1);
        break;
    }
}
EI_REPLACE_METHOD(0x00602C90, CBriefingScreen::OnUnitJoined);

// --- 00603010: slot 30 --------------------------------------------------------------------------

void CBriefingScreen::OnUnitLeft(CUnit* unit) {
    if (unit == leader_) leader_ = nullptr;
    int count = party_.size;
    int index = 0;
    while (index < count && reinterpret_cast<CUnit*>(party_.data[index])->id_ != unit->id_) ++index;
    if (index >= count) return;
    int after = count - index - 1;
    if (after != 0) std::memmove(&party_.data[index], &party_.data[index + 1], after * sizeof(party_.data[0]));
    --party_.size;
}
EI_REPLACE_METHOD(0x00603010, CBriefingScreen::OnUnitLeft);

// --- 00608C70: slot 31 --------------------------------------------------------------------------

void CBriefingScreen::OnGameOver(int) {
    interface_->PlaySound("buttons\\gameover.wav", 0);
    void* memory = gamecrt::operator_new(sizeof(CMessageBox));
    void* box = memory ? static_cast<CMessageBox*>(memory)->Construct(interface_, "game_over",
                                                     "game_over_msg", kGameOverResult, 3, 1)
                       : nullptr;
    interface_->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
}
EI_REPLACE_METHOD(0x00608C70, CBriefingScreen::OnGameOver);

// --- 006030C0: slot 34 --------------------------------------------------------------------------

void CBriefingScreen::OnBriefing(const char* name) {
    topic_ = name;
    StartDialogue(1, 1);
}
EI_REPLACE_METHOD(0x006030C0, CBriefingScreen::OnBriefing);

// --- 00603090: slot 36 --------------------------------------------------------------------------

void CBriefingScreen::ShowChat(char show) {
    if (IsMultiplayerClient() && show && chat_) ChatWindowShowAt(chat_);
}
EI_REPLACE_METHOD(0x00603090, CBriefingScreen::ShowChat);

// --- 00602490: slot 40 --------------------------------------------------------------------------

void CBriefingScreen::SaveState() {
    int zero = 0;
    GameSaveStream()->Write(&zero, 4);
}
EI_REPLACE_METHOD(0x00602490, CBriefingScreen::SaveState);
// 0062D330: the global map's slot 40, byte-identical (one zero dword).
EI_REPLACE_METHOD(0x0062D330, CBriefingScreen::SaveState);
EI_REPLACE_METHOD(0x00606F40, CBriefingScreen::OnMessageResult);

// --- 00607720 -----------------------------------------------------------------------------------

void CBriefingScreen::LoadPersonTexts() { ::LoadPersonTexts(&personTexts_); }
EI_REPLACE_METHOD(0x00607720, CBriefingScreen::LoadPersonTexts);
