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
#include <vector>

#include "lib/gameapi.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

int FinishQuestMap(const char* name) {
    net::ServerSession* server = net::g_gameServer;
    if (!server || (server->flags7_ & CRefCounted::kDetached)) return 0;
    std::string text(name);
    return server->FinishMap(&text) ? 1 : 0;
}

namespace {
CScript* CommandScript() { return g_serverWorld.SecondScript(); }

constexpr unsigned kFolderBufferSize = 0x400;

constexpr double kHoursPerDay = 24.0;
const char kConstructionVariable[] = "constr.current";
// Every window closes before the next input message or by the next pass (CloseAllPending, Activate, Draw).
const char* DefaultMusic() { return options::g_options.defaultStreamingSound_.c_str(); }
CPlayerClient* LocalClient() { return g_clientWorld.LocalView(); }

} // namespace

int RunScriptText(const std::string& text, char run, ScriptErrorInfo* error) {
    CScript* script = CommandScript();
    std::string source = text;   // CScript::Load preprocesses its text in place
    int loaded = script->Load(&source, 1);
    if (loaded && run) ScriptTick(script);
    if (error) {
        ScriptErrorInfo last{script->errorText_, script->errorCode_, script->errorLine_};
        error->text = last.text;
        error->code = last.code;
        error->line = last.line;
    }
    return loaded;
}

void PlayerZeroVariables(const char* prefix, NamedFloatTable* out) {
    CPlayer* player;
    if (IsMultiplayerClient()) {
        player = g_clientWorld.LocalView()->player_;
    } else {
        player = g_serverWorld.GetPlayer(0);
        if (!player) {
            out->clear();
            return;
        }
    }
    player->Party()->variables_.CopyPrefixed(prefix, out);
}

void StoreStartFolder() {
    char folder[kFolderBufferSize];
    gameapi::GetCurrentDirectoryA(kFolderBufferSize, folder);
    std::string text = folder;
    g_startFolder = text + '\\';
}

// ---- A-W13: the rest of the glue -----------------------------------------------------------------

void GameSetConstructionVariable(std::uint32_t value) {
    CPlayer* player = g_serverWorld.GetPlayer(0);
    if (player) player->Server()->SetVariable(kConstructionVariable, value);
}

void GameSetWeather(int weather, float strength) { g_serverWorld.SetWeather(weather, strength); }

double GameServerTimeOfDay() {
    CWorldServer* world = &g_serverWorld;
    return std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, kHoursPerDay);
}

void GamePassTime(float offset) {
    // Nothing for a negative offset or a NaN.
    if (offset >= 0.0f) g_serverWorld.UpdatePlayerTimes(offset);
}

void GameEnsureZone(std::string_view map, std::string_view mob) { g_serverWorld.EnsureZone(map, mob); }

void GameSetUnitHeight(int id, float z) {
    CWorldServer* world = &g_serverWorld;
    auto* unit = world->FindUnit(id);
    if (!unit) return;
    // Nothing when the heights are equal.
    EI_NAN_AUDIT(unit->localPosition_.z, z);
    if (unit->localPosition_.z == z) return;
    Vector3 target = {unit->localPosition_.x, unit->localPosition_.y, z};
    world->aiMap_.sectorMap.Move(unit, target);
    unit->MarkChanged(2);
}

void GameSetServerPaused(int paused) { g_serverWorld.SetServerPaused(static_cast<std::uint8_t>(paused)); }

void GameStopCameraRecording() {
    CWorldClient* world = &g_clientWorld;
    CameraCloseFile(world->camera_);
    world->flags_ &= ~WorldClientFlag::RecordingCamera;
}

int GameStartCameraRecording(const char* name) {
    CWorldClient* world = &g_clientWorld;
    world->flags_ |= WorldClientFlag::RecordingCamera;
    return world->camera_->OpenFile(name);
}

int GamePlayCameraPath(const char* name, int flag) {
    CWorldClient* world = &g_clientWorld;
    if (!world->camera_->LoadPath(name, flag)) return 0;
    CCamera* camera = world->camera_;
    camera->flags_ |= CCamera::kPlaying;
    camera->keyIndex_ = 0;
    camera->timeOffset_ = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_ - camera->keys_[0].time);
    return 1;
}

int GameShowCameraPath(const char* name) {
    CWorldClient* world = &g_clientWorld;
    int loaded = world->camera_->LoadPath(name, 0);
    if (!loaded) return loaded;
    return world->camera_->PlaybackStart();
}

std::uint32_t GameCameraPathPlaying() { return g_clientWorld.camera_->flags_ & CCamera::kPlaying; }

// The client's draw is called through its address: Rebirth patches it (LandscapeDrawRadius; rule 9).
int GameRender() { return g_clientWorld.Render(); }

void GameStopSounds(int stopStream) { g_clientWorld.sounds_->StopAll(static_cast<std::uint8_t>(stopStream) != 0); }

void GameStopAmbience() { g_clientWorld.terrain_->StopAmbience(); }

void GameStartAmbience() { g_clientWorld.terrain_->landscapeFlags |= LandscapeFlag::Ambience; }

int GamePlayMusic(const char* name) {
    return g_clientWorld.sounds_->RequestStream(name ? name : DefaultMusic(), 4);
}

CUnit* GameFindClientUnit(int id) { return g_clientWorld.FindUnit(id); }

void GameCreatePartyUnits(const std::vector<PartyUnitRecord*>* party, int allArmour) {
    g_clientWorld.CreatePartyUnits(party, static_cast<char>(allArmour));
}

void GameRemoveUnitsOf(const std::vector<PartyUnitRecord*>* records) { g_clientWorld.RemoveUnitsOf(records); }

int GameFindExitAt(const float* point, int* index) { return g_clientWorld.FindExitAt(point, index); }

int GamePartyInExit(const std::vector<PartyUnitRecord*>* records, int exit, std::vector<PartyUnitRecord*>* inExit) {
    return g_clientWorld.PartyInExit(records, exit, inExit);
}

const char* GameExitTarget(int index) { return g_clientWorld.ExitTarget(index); }

void GameLeaveThroughExit(int exit) {
    if (IsMultiplayerClient())
        g_gameFlow.SwitchMultiplayerZone();
    else
        LocalClient()->TakeExit(exit);
}

void GameSendLeave04(int value) {
    g_interface.closeAll_ = 1;
    LocalClient()->SendLeave04(value);
}

void GameRejoin() {
    g_interface.closeAll_ = 1;
    LocalClient()->Rejoin();
}

void GameLocalPlayerVariables(std::string_view prefix, NamedFloatTable* out) {
    CPlayer* player = g_clientWorld.LocalView()->player_;
    player->Party()->variables_.CopyPrefixed(prefix, out);
}

CPlayerClient* GameLocalView() { return g_clientWorld.LocalView(); }

int GameSendFlag10(int value) { return g_clientWorld.SendFlag10(value); }

