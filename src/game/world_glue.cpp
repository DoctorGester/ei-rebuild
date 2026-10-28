// The game glue's small functions. See world_glue.h and docs/notes/world.md.
#include "world_glue.h"
#include "options.h"

#include "net_classes.h"
#include "object_tables.h"
#include "script.h"
#include "party.h"
#include "world.h"
#include "world_data.h"
#include "ui.h"

#include "camera.h"
#include "player_client.h"
#include "player_messages.h"
#include "sector_map.h"
#include "sound.h"
#include "terrain.h"
#include "unit.h"

#include <cmath>
#include <string>

#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

const mfc::CString* GameDataPathString() {
    return &TheDataFolder();
}

const mfc::CString* GameBasePathString() {
    return &TheDiscFolder();
}

int __stdcall FinishQuestMap(const char* name) {
    net::ServerSession* server = net::GameServer();
    if (!server || (server->flags7_ & CRefCounted::kDetached)) return 0;
    stlp::string text(name);
    return server->FinishMap(&text) ? 1 : 0;
}

namespace {
CScript* CommandScript() { return TheServerWorld()->SecondScript(); }
} // namespace

int __stdcall RunScriptText(mfc::CString* text, char run, ScriptErrorInfo* error) {
    CScript* script = CommandScript();
    int loaded = script->Load(text, 1);
    if (loaded && run) ScriptTick(script);
    if (error) {
        ScriptErrorInfo last{script->ErrorText(), script->ErrorCode(), script->ErrorLine()};
        error->text = last.text;
        error->code = last.code;
        error->line = last.line;
    }
    return loaded;
}

void __stdcall PlayerZeroVariables(const char* prefix, NamedFloatTable* out) {
    CPlayer* player;
    if (IsMultiplayerClient()) {
        player = TheClientWorld()->LocalView()->player_;
    } else {
        player = TheServerWorld()->GetPlayer(0);
        if (!player) {
            out->Clear();
            return;
        }
    }
    player->Party()->variables_.CopyPrefixed(prefix, out);
}

namespace {
mfc::CString& StartFolder() { return TheStartFolder(); }
constexpr unsigned kFolderBufferSize = 0x400;
} // namespace

void __cdecl StoreStartFolder() {
    char folder[kFolderBufferSize];
    gameapi::GetCurrentDirectoryA(kFolderBufferSize, folder);
    std::string text = folder;
    StartFolder() = (mfc::operator+(text, '\\')).GetString();
}

// ---- A-W13: the rest of the glue -----------------------------------------------------------------

namespace {
constexpr double kHoursPerDay = 24.0;
const char kConstructionVariable[] = "constr.current";
// Every window closes before the next input message or by the next pass (CloseAllPending, Activate, Draw).
bool& CloseAllRequested() { return TheInterface()->closeAll_; }
float& MapEdgeFade() { return options::TheOptions()->borderFogDistance_; }
const char* DefaultMusic() { return options::TheOptions()->defaultStreamingSound_.c_str(); }
constexpr std::uint32_t kClientRecordingCamera = 0x10;   // CWorldClient flags_
constexpr std::uint32_t kTerrainAmbience = 8;            // Terrain landscapeFlags
CPlayerClient* LocalClient() { return TheClientWorld()->LocalView(); }
} // namespace

void __stdcall GameSetConstructionVariable(std::uint32_t value) {
    CPlayer* player = TheServerWorld()->GetPlayer(0);
    if (player) player->Server()->SetVariable(kConstructionVariable, value);
}

void __stdcall GameSetWeather(int weather, float strength) { TheServerWorld()->SetWeather(weather, strength); }

double GameServerTimeOfDay() {
    CWorldServer* world = TheServerWorld();
    return std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, kHoursPerDay);
}

void __stdcall GamePassTime(float offset) {
    // FCOMP 0 with TEST AH,1: nothing for a negative offset or a NaN.
    if (offset >= 0.0f) TheServerWorld()->UpdatePlayerTimes(offset);
}

void __stdcall GameEnsureZone(const char* map, const char* mob) { TheServerWorld()->EnsureZone(map, mob); }

CMemoryStream* GameObject7C2D60() { return &TheServerWorld()->zoneHeader_; }

void __stdcall GameSetUnitHeight(int id, float z) {
    CWorldServer* world = TheServerWorld();
    auto* unit = world->FindUnit(id);
    if (!unit) return;
    // Nothing when the heights are equal.
    EI_NAN_AUDIT(unit->localPosition_.z, z);
    if (unit->localPosition_.z == z) return;
    Vector3 target = {unit->localPosition_.x, unit->localPosition_.y, z};
    world->Sectors()->Move(unit, &target);
    unit->MarkChanged(2);
}

std::uint8_t GameServerPaused() { return TheServerWorld()->paused_; }

void __stdcall GameSetServerPaused(int paused) { TheServerWorld()->SetServerPaused(static_cast<std::uint8_t>(paused)); }

void GameStopCameraRecording() {
    CWorldClient* world = TheClientWorld();
    CameraCloseFile(world->camera_);
    world->flags_ &= ~kClientRecordingCamera;
}

int __stdcall GameStartCameraRecording(const char* name) {
    CWorldClient* world = TheClientWorld();
    world->flags_ |= kClientRecordingCamera;
    return world->camera_->OpenFile(name);
}

int __stdcall GamePlayCameraPath(const char* name, int flag) {
    CWorldClient* world = TheClientWorld();
    if (!world->camera_->LoadPath(name, flag)) return 0;
    CCamera* camera = world->camera_;
    camera->flags_ |= CCamera::kPlaying;
    camera->keyIndex_ = 0;
    camera->timeOffset_ = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_ - camera->keys_[0].time);
    return 1;
}

int __stdcall GameShowCameraPath(const char* name) {
    CWorldClient* world = TheClientWorld();
    int loaded = world->camera_->LoadPath(name, 0);
    if (!loaded) return loaded;
    return world->camera_->PlaybackStart();
}

std::uint32_t GameCameraPathPlaying() { return TheClientWorld()->camera_->flags_ & CCamera::kPlaying; }

// The client's draw is called through its address: Rebirth patches it (LandscapeDrawRadius; rule 9).
int GameRender() { return TheClientWorld()->Render(); }

void __stdcall GameStopSounds(int stopStream) { TheClientWorld()->sounds_->StopAll(static_cast<std::uint8_t>(stopStream) != 0); }

void GameStopAmbience() { TheClientWorld()->terrain_->StopAmbience(); }

void GameStartAmbience() { TheClientWorld()->terrain_->landscapeFlags |= kTerrainAmbience; }

int __stdcall GamePlayMusic(const char* name) {
    return TheClientWorld()->sounds_->RequestStream(name ? name : DefaultMusic(), 4);
}

CUnit* __stdcall GameFindClientUnit(int id) { return TheClientWorld()->FindUnit(id); }

void __stdcall GameCreatePartyUnits(const mfc::CArray<PartyUnitRecord*>* party, int allArmour) {
    TheClientWorld()->CreatePartyUnits(party, static_cast<char>(allArmour));
}

void __stdcall GameRemoveUnitsOf(const mfc::CArray<PartyUnitRecord*>* records) { TheClientWorld()->RemoveUnitsOf(records); }

int __stdcall GameFindExitAt(const float* point, int* index) { return TheClientWorld()->FindExitAt(point, index); }

int __stdcall GamePartyInExit(const mfc::CArray<PartyUnitRecord*>* records, int exit, mfc::CArray<PartyUnitRecord*>* inExit) {
    return TheClientWorld()->PartyInExit(records, exit, inExit);
}

const char* __stdcall GameExitTarget(int index) { return TheClientWorld()->ExitTarget(index); }

void __stdcall GameLeaveThroughExit(int exit) {
    if (IsMultiplayerClient())
        TheGameFlow()->SwitchMultiplayerZone();
    else
        LocalClient()->TakeExit(exit);
}

void __stdcall GameSendLeave04(int value) {
    CloseAllRequested() = 1;
    LocalClient()->SendLeave04(value);
}

void GameRejoin() {
    CloseAllRequested() = 1;
    LocalClient()->Rejoin();
}

void __stdcall GameLocalPlayerVariables(const char* prefix, NamedFloatTable* out) {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    player->Party()->variables_.CopyPrefixed(prefix, out);
}

CPlayerClient* GameLocalView() { return TheClientWorld()->LocalView(); }

int __stdcall GameSendFlag10(int value) { return TheClientWorld()->SendFlag10(value); }

std::uint8_t GameClientPaused() { return TheClientWorld()->paused_; }

CTextManager* GameClientTexts() { return TheClientWorld()->texts_; }
