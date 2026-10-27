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

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

const mfc::CString* GameDataPathString() {
    return &TheDataFolder();
}
EI_REWRITTEN(0x00578DB0, GameDataPathString);

const mfc::CString* GameBasePathString() {
    return &TheDiscFolder();
}
EI_REWRITTEN(0x00578DC0, GameBasePathString);

int __stdcall FinishQuestMap(const char* name) {
    net::ServerSession* server = net::GameServer();
    if (!server || (server->flags7_ & CRefCounted::kDetached)) return 0;
    stlp::string text(name);
    return server->FinishMap(&text) ? 1 : 0;
}
EI_REWRITTEN(0x0057FB90, FinishQuestMap);

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
EI_REWRITTEN(0x005785D0, RunScriptText);

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
EI_REWRITTEN(0x00578C40, PlayerZeroVariables);

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
EI_REWRITTEN(0x0055EA10, StoreStartFolder);

// ---- A-W13: the rest of the glue -----------------------------------------------------------------

namespace {
constexpr double kHoursPerDay = 24.0;
const char kConstructionVariable[] = "constr.current";
// The interface's close-all flag (CInterface3D +0x44): every window closes before the next input message or
// by the next pass (CloseAllPending, Activate, Draw).
bool& CloseAllRequested() { return TheInterface()->closeAll_; }
float& MapEdgeFade() { return options::TheOptions()->borderFogDistance_; }
const char* DefaultMusic() { return options::TheOptions()->defaultStreamingSound_.GetString(); }
constexpr std::uint32_t kClientRecordingCamera = 0x10;   // CWorldClient flags_
constexpr std::uint32_t kTerrainAmbience = 8;            // Terrain landscapeFlags
CPlayerClient* LocalClient() { return TheClientWorld()->LocalView(); }
} // namespace

void __stdcall GameSetConstructionVariable(std::uint32_t value) {
    CPlayer* player = TheServerWorld()->GetPlayer(0);
    if (player) player->Server()->SetVariable(kConstructionVariable, value);
}
EI_REWRITTEN(0x00578560, GameSetConstructionVariable);

void __stdcall GameSetWeather(int weather, float strength) { TheServerWorld()->SetWeather(weather, strength); }
EI_REWRITTEN(0x005786A0, GameSetWeather);

double GameServerTimeOfDay() {
    CWorldServer* world = TheServerWorld();
    return std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, kHoursPerDay);
}
EI_REWRITTEN(0x005786E0, GameServerTimeOfDay);

void __stdcall GamePassTime(float offset) {
    // FCOMP 0 with TEST AH,1: nothing for a negative offset or a NaN.
    if (offset >= 0.0f) TheServerWorld()->UpdatePlayerTimes(offset);
}
EI_REWRITTEN(0x00578700, GamePassTime);

void __stdcall GameEnsureZone(const char* map, const char* mob) { TheServerWorld()->EnsureZone(map, mob); }
EI_REWRITTEN(0x00578730, GameEnsureZone);

CMemoryStream* GameObject7C2D60() { return &TheServerWorld()->zoneHeader_; }
EI_REWRITTEN(0x00578750, GameObject7C2D60);

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
EI_REWRITTEN(0x00578760, GameSetUnitHeight);

std::uint8_t GameServerPaused() { return TheServerWorld()->paused_; }
EI_REWRITTEN(0x00578D00, GameServerPaused);

void __stdcall GameSetServerPaused(int paused) { TheServerWorld()->SetServerPaused(static_cast<std::uint8_t>(paused)); }
EI_REWRITTEN(0x00578D10, GameSetServerPaused);

void GameStopCameraRecording() {
    CWorldClient* world = TheClientWorld();
    CameraCloseFile(world->camera_);
    world->flags_ &= ~kClientRecordingCamera;
}
EI_REWRITTEN(0x005787D0, GameStopCameraRecording);

int __stdcall GameStartCameraRecording(const char* name) {
    CWorldClient* world = TheClientWorld();
    world->flags_ |= kClientRecordingCamera;
    return world->camera_->OpenFile(name);
}
EI_REWRITTEN(0x005787F0, GameStartCameraRecording);

int __stdcall GamePlayCameraPath(const char* name, int flag) {
    CWorldClient* world = TheClientWorld();
    if (!world->camera_->LoadPath(name, flag)) return 0;
    CCamera* camera = world->camera_;
    camera->flags_ |= CCamera::kPlaying;
    camera->keyIndex_ = 0;
    camera->timeOffset_ = static_cast<float>(static_cast<double>(world->tick_) + world->tickFraction_ - camera->keys_[0].time);
    return 1;
}
EI_REWRITTEN(0x00578820, GamePlayCameraPath);

int __stdcall GameShowCameraPath(const char* name) {
    CWorldClient* world = TheClientWorld();
    int loaded = world->camera_->LoadPath(name, 0);
    if (!loaded) return loaded;
    return world->camera_->PlaybackStart();
}
EI_REWRITTEN(0x00578880, GameShowCameraPath);

std::uint32_t GameCameraPathPlaying() { return TheClientWorld()->camera_->flags_ & CCamera::kPlaying; }
EI_REWRITTEN(0x005788B0, GameCameraPathPlaying);

// The client's draw is called through its address: Rebirth patches it (LandscapeDrawRadius; rule 9).
int GameRender() { return static_cast<CWorldClient*>(TheClientWorld())->Render(); }
EI_REWRITTEN(0x00578910, GameRender);

void __stdcall GameStopSounds(int stopStream) { TheClientWorld()->sounds_->StopAll(static_cast<std::uint8_t>(stopStream) != 0); }
EI_REWRITTEN(0x00578920, GameStopSounds);

void GameStopAmbience() { TheClientWorld()->terrain_->StopAmbience(); }
EI_REWRITTEN(0x00578940, GameStopAmbience);

void GameStartAmbience() { TheClientWorld()->terrain_->landscapeFlags |= kTerrainAmbience; }
EI_REWRITTEN(0x00578950, GameStartAmbience);

int __stdcall GamePlayMusic(const char* name) {
    return TheClientWorld()->sounds_->RequestStream(name ? name : DefaultMusic(), 4);
}
EI_REWRITTEN(0x00578960, GamePlayMusic);

CUnit* __stdcall GameFindClientUnit(int id) { return TheClientWorld()->FindUnit(id); }
EI_REWRITTEN(0x00578980, GameFindClientUnit);

void __stdcall GameCreatePartyUnits(const mfc::CArray<PartyUnitRecord*>* party, int allArmour) {
    TheClientWorld()->CreatePartyUnits(party, static_cast<char>(allArmour));
}
EI_REWRITTEN(0x005789C0, GameCreatePartyUnits);

void __stdcall GameRemoveUnitsOf(const mfc::CArray<PartyUnitRecord*>* records) { TheClientWorld()->RemoveUnitsOf(records); }
EI_REWRITTEN(0x005789E0, GameRemoveUnitsOf);

int __stdcall GameFindExitAt(const float* point, int* index) { return TheClientWorld()->FindExitAt(point, index); }
EI_REWRITTEN(0x00578A00, GameFindExitAt);

int __stdcall GamePartyInExit(const mfc::CArray<PartyUnitRecord*>* records, int exit, mfc::CArray<PartyUnitRecord*>* inExit) {
    return TheClientWorld()->PartyInExit(records, exit, inExit);
}
EI_REWRITTEN(0x00578A20, GamePartyInExit);

const char* __stdcall GameExitTarget(int index) { return TheClientWorld()->ExitTarget(index); }
EI_REWRITTEN(0x00578A40, GameExitTarget);

void __stdcall GameLeaveThroughExit(int exit) {
    if (IsMultiplayerClient())
        TheGameFlow()->SwitchMultiplayerZone();
    else
        LocalClient()->TakeExit(exit);
}
EI_REWRITTEN(0x00578AC0, GameLeaveThroughExit);

void __stdcall GameSendLeave04(int value) {
    CloseAllRequested() = 1;
    LocalClient()->SendLeave04(value);
}
EI_REWRITTEN(0x00578B00, GameSendLeave04);

void GameRejoin() {
    CloseAllRequested() = 1;
    LocalClient()->Rejoin();
}
EI_REWRITTEN(0x00578B20, GameRejoin);

void __stdcall GameLocalPlayerVariables(const char* prefix, NamedFloatTable* out) {
    CPlayer* player = TheClientWorld()->LocalView()->player_;
    player->Party()->variables_.CopyPrefixed(prefix, out);
}
EI_REWRITTEN(0x00578B70, GameLocalPlayerVariables);

CPlayerClient* GameLocalView() { return TheClientWorld()->LocalView(); }
EI_REWRITTEN(0x00578BC0, GameLocalView);

int __stdcall GameSendFlag10(int value) { return TheClientWorld()->SendFlag10(value); }
EI_REWRITTEN(0x00578D30, GameSendFlag10);

std::uint8_t GameClientPaused() { return TheClientWorld()->paused_; }
EI_REWRITTEN(0x00578D50, GameClientPaused);

CTextManager* GameClientTexts() { return TheClientWorld()->texts_; }
EI_REWRITTEN(0x00578E20, GameClientTexts);

