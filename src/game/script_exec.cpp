// Script command execution: ScriptRun::Execute (004B6760), the switch over every world script
// command. See script.h and docs/notes/script.md.
//
// Rebirth patches Execute in memory (it adds commands), so this replacement installs only in the
// vanilla game. Every callee is reached through its original address (forwarders below): several
// are patched by Rebirth (CalcParameters, FatalError, 005935C0, Execute itself for For bodies), and
// the rest still run original code or their own replacement's verify wrapper.
#include "ai_map.h"
#include "object_tables.h"
#include "script.h"
#include "game_strings.h"
#include "script_addon.h"

#include "combat.h"
#include "console.h"
#include "item.h"
#include "map_object.h"
#include "net_classes.h"
#include "object_diplomacy.h"
#include "party.h"
#include "party_manager.h"
#include "party_records.h"
#include "player_messages.h"
#include "quest_records.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_render.h"
#include "originals_ai.h"
#include "originals_core.h"

#include <cstdint>
#include <cstring>
#include <bit>

namespace {

// ---- x87 helpers --------------------------------------------------------------------------------

// The game's _ftol (006E853C): truncating conversion, low 32 bits of the 64-bit result.
int Ftol(float value) { return static_cast<int>(gamecrt::Ftol(value)); }

// FCOMP against the constant 0.0 at 0073B858; true when C3 is set (equal or unordered), the test
// every command uses for "flag argument is zero".
bool ZeroOrNaN(float value) { return (x87::CompareStatus(value, 0.0) & x87::kC3) != 0; }

// FCOM a, b: the status bits C0 (0x01, less or unordered) and C3 (0x40, equal or unordered).
int FcomStatus(float a, float b) { return x87::CompareStatus(a, b); }

int Round(float value) { return x87::fistp(value); }

// ---- Original code reached by the commands ------------------------------------------------------
//
// The callees as the original reaches them: thiscall entry points on the game's objects, declared
// as members of call-only facades (`...Calls`, no fields) over the real objects, so each call goes
// through the original address.

constexpr int kClassLever = 0x5D;
constexpr int kClassQuestItem = 0x14;

// Game texts.
const char* const kKillScriptNotAllowed = "KillScript command not allowed here";   // 0078B904
const char* MoviesFolder() { return reinterpret_cast<const char*>(0x00784A4C); }   // "Movies\"
// 0073C36C: 1 / 255, the background colour scale.
float ColourScale() { return *reinterpret_cast<const float*>(0x0073C36C); }
// 0079BE5C: the configured world time speed (an int).
int ConfiguredTimeSpeed() { return *reinterpret_cast<const int*>(0x0079BE5C); }

// Script helpers (script.h types, original addresses).
__declspec(naked) void __fastcall CalcParametersOrig(SSCCmd*, void*, CScript*, ScriptVars*, const ScriptSubCall*)
    EI_ORIGINAL(0x004B4030)
// CArray<ScriptObjectRef> (groups): Copy 004C1290, RemoveAt 004C2200.
inline void __fastcall GroupCopy(ScriptGroup* a0, void*, const ScriptGroup* a2) { GroupAssignOriginal((void*)a0, nullptr, (const void*)a2); }
__declspec(naked) void __fastcall GroupRemoveAt(ScriptGroup*, void*, int, int) EI_ORIGINAL(0x004C2200)
// CList<ScriptDefinition> AddHead (CScript::instances_; despite the AddTail-like shape it links the
// new node before the head): returns the new node.
using InstanceList = mfc::CList<ScriptDefinition>;
__declspec(naked) InstanceList::Node* __fastcall InstanceAddHead(InstanceList*, void*, const ScriptDefinition*) EI_ORIGINAL(0x004C7C90)
// CList RemoveAt for the particle, FX source and lightning lists.
using ParticleList = mfc::CList<ScriptParticleSource>;
using FxSourceList = mfc::CList<ScriptFxSource>;
using LightningList = mfc::CList<ScriptLightning>;
__declspec(naked) void __fastcall ParticleRemoveAt(ParticleList*, void*, ParticleList::Node*) EI_ORIGINAL(0x004C6040)
__declspec(naked) void __fastcall FxSourceRemoveAt(FxSourceList*, void*, FxSourceList::Node*) EI_ORIGINAL(0x004C6370)
__declspec(naked) void __fastcall LightningRemoveAt(LightningList*, void*, LightningList::Node*) EI_ORIGINAL(0x004C6680)
// Areas (FindArea's result): add a rectangle / a circle.
__declspec(naked) void __fastcall AreaAddRect(void*, void*, float, float, float, float) EI_ORIGINAL(0x004B1ED0)
__declspec(naked) void __fastcall AreaAddRound(void*, void*, float, float, float) EI_ORIGINAL(0x004B2070)
// The party records of `player` for a "party name" string (ECX player, EDX string).
// Object id of a name (ECX = name).
// 005DB3F0: plays a movie file (ECX path, EDX 0, five flags).
// 00500960: the renderer (returns the object whose 004BA120 sets the background colour).
void* __cdecl Renderer() { return DisplayObject(); }

// The world server.
struct WorldCalls : WorldAlarmOriginal {
    WorldPlayer* GetPlayer(int index);                                   // 00594180
    float GridHeight(float x, float y);                                  // 00594220
    void AddUnit(int key, const char* prototype, const char* name, const Vector3* point);   // 005910A0
    void RemoveObject(int id);                                           // 005935C0 (Rebirth patches it)
    void RecalcMercBriefings();                                          // 00591800
    void SetWind(const Vector3* direction, float strength);              // 0058EE20
    void SetTimeSpeed(float speed);                                      // 0058EEE0
    void SetTime(float time);                                            // 0058EEA0
    void SetWaterLevel(int a, float level, int b);                       // 00593FA0
    CEngineObject* FindItem(const char* name);                           // 00594AB0
    CEngineObject* FindSpell(const char* name);                          // 00594BC0
    void CollectParty(WorldPlayer* player, void* list);                  // 00590A40
    void Redeploy(WorldPlayer* player, void* list, int flag);            // 00590700
    void AwardKill(WorldPlayer* player, std::uint32_t exp);              // 005911C0 (the amount as float bits)
    void CastSpell(CEngineObject* spell, CEngineObject* caster, const Vector3* from, CEngineObject* target, const Vector3* to, int extra);  // 0058E850
};
WorldPlayer* WorldCalls::GetPlayer(int id) { return static_cast<WorldPlayer*>(::WorldGetPlayer(this, nullptr, id)); }
__declspec(naked) float WorldCalls::GridHeight(float, float) EI_ORIGINAL(0x00594220)
__declspec(naked) void WorldCalls::AddUnit(int, const char*, const char*, const Vector3*) EI_ORIGINAL(0x005910A0)
void WorldCalls::RemoveObject(int id) { WorldRemoveObject(this, nullptr, id); }
void WorldCalls::RecalcMercBriefings() { WorldUpdateMercBriefingsOriginal(this); }
__declspec(naked) void WorldCalls::SetWind(const Vector3*, float) EI_ORIGINAL(0x0058EE20)
void WorldCalls::SetTimeSpeed(float a0) { WorldSetTimeSpeedOriginal((void*)this, nullptr, std::bit_cast<std::uint32_t>(a0)); }
void WorldCalls::SetTime(float a0) { WorldSetTimeOriginal((void*)this, nullptr, std::bit_cast<std::uint32_t>(a0)); }
__declspec(naked) void WorldCalls::SetWaterLevel(int, float, int) EI_ORIGINAL(0x00593FA0)
__declspec(naked) CEngineObject* WorldCalls::FindItem(const char*) EI_ORIGINAL(0x00594AB0)
__declspec(naked) CEngineObject* WorldCalls::FindSpell(const char*) EI_ORIGINAL(0x00594BC0)
__declspec(naked) void WorldCalls::CollectParty(WorldPlayer*, void*) EI_ORIGINAL(0x00590A40)
__declspec(naked) void WorldCalls::Redeploy(WorldPlayer*, void*, int) EI_ORIGINAL(0x00590700)
void WorldCalls::AwardKill(WorldPlayer* player, std::uint32_t exp) { WorldAwardExperienceOriginal(this, nullptr, player, exp); }
void WorldCalls::CastSpell(CEngineObject* spell, CEngineObject* caster, const Vector3* from, CEngineObject* target, const Vector3* to, int extra) { WorldCastSpellOriginal(this, nullptr, spell, caster, from, target, to, extra); }
WorldCalls* TheWorld() { return reinterpret_cast<WorldCalls*>(WorldServer()); }

// The world's AI map (CWorldServer +0x170); its sector map is at +0x124.
struct AIMapCalls {
    void Move(CEngineObject* object, const Vector3* point);                 // 004BA0E0
    void FindUnitsInRadius(const Vector3* point, void* list, float radius); // 004BA100
    void Remove(CEngineObject* object);                                     // 004BA0C0
    void Add(CEngineObject* object);                                        // 004BA0A0
    bool CanLeaveCell(CEngineObject* object);                               // 005BA830
    bool FindFreeCellNear(CEngineObject* object, const Vector3* from, Vector3* out);   // 005BA9D0
    char SetUpdates(int on);                                                // 004BA090 (byte +2)
};
__declspec(naked) void AIMapCalls::Move(CEngineObject*, const Vector3*) EI_ORIGINAL(0x004BA0E0)
__declspec(naked) void AIMapCalls::FindUnitsInRadius(const Vector3*, void*, float) EI_ORIGINAL(0x004BA100)
__declspec(naked) void AIMapCalls::Remove(CEngineObject*) EI_ORIGINAL(0x004BA0C0)
__declspec(naked) void AIMapCalls::Add(CEngineObject*) EI_ORIGINAL(0x004BA0A0)
bool AIMapCalls::CanLeaveCell(CEngineObject* object) { return AIMapCanLeaveCellOriginal(this, nullptr, static_cast<CUnit*>(object)); }
bool AIMapCalls::FindFreeCellNear(CEngineObject* object, const Vector3* from, Vector3* out) { return reinterpret_cast<CAIMap*>(this)->FindFreeCellNear(static_cast<CUnit*>(object), from, out); }
__declspec(naked) char AIMapCalls::SetUpdates(int) EI_ORIGINAL(0x004BA090)
AIMapCalls* TheAIMap() { return reinterpret_cast<AIMapCalls*>(WorldServer()->AIMap()); }

// A unit's AI brain (CUnitServer::Brain()).
struct BrainCalls {
    void Walk(const Vector3* point, int a, int b);       // 005D7AE0
    void TurnTowards(const Vector3* direction, int a);   // 005D7B30
    void Follow(CEngineObject* leader);                  // 005D7E40
    void AttackTarget(CEngineObject* target);            // 005D7B50
    void CastAt(int a, const Vector3* point, int b);     // 005D7DA0
    void ResetTarget();                                  // 005D7C30
    void ClearModes();                                   // 005D4DF0
    void StopAction();                                   // 005D7790
    void Guard(const Vector3* point, float radius);      // 005D6230
    void Sentry(const Vector3* point);                   // 005D64E0
    void AddMode(void* mode);                            // 005D5F60
    void* FindPatrolMode();                              // 005D5E30
    void RemoveMode(int type);                           // 005D6140
};
__declspec(naked) void BrainCalls::Walk(const Vector3*, int, int) EI_ORIGINAL(0x005D7AE0)
__declspec(naked) void BrainCalls::TurnTowards(const Vector3*, int) EI_ORIGINAL(0x005D7B30)
__declspec(naked) void BrainCalls::Follow(CEngineObject*) EI_ORIGINAL(0x005D7E40)
__declspec(naked) void BrainCalls::AttackTarget(CEngineObject*) EI_ORIGINAL(0x005D7B50)
__declspec(naked) void BrainCalls::CastAt(int, const Vector3*, int) EI_ORIGINAL(0x005D7DA0)
__declspec(naked) void BrainCalls::ResetTarget() EI_ORIGINAL(0x005D7C30)
void BrainCalls::ClearModes() { static_cast<AIBrain*>((void*)this)->ClearModes(); }
void BrainCalls::StopAction() { BrainStopActionOriginal(this); }
__declspec(naked) void BrainCalls::Guard(const Vector3*, float) EI_ORIGINAL(0x005D6230)
__declspec(naked) void BrainCalls::Sentry(const Vector3*) EI_ORIGINAL(0x005D64E0)
void BrainCalls::AddMode(void* a0) { reinterpret_cast<AIBrain*>(this)->AddMode(static_cast<AIMode*>(a0)); }
__declspec(naked) void* BrainCalls::FindPatrolMode() EI_ORIGINAL(0x005D5E30)
void BrainCalls::RemoveMode(int a0) { static_cast<AIBrain*>((void*)this)->RemoveModesOfType(a0); }

// Unit modes (the brain's behaviour list): allocated by the command (NewMode) and constructed by
// these.
struct ModeCalls {
    void* vtable;
    int refCount;                                        // +0x04 written 0 by the command first
    ModeCalls* Sentry(const Vector3* point);             // 004BA360 (0x14 bytes)
    ModeCalls* Guard(const Vector3* point, float r1, float r2);   // 004BA1C0 (0x20)
    ModeCalls* Follow(CEngineObject* leader);            // 004BA450 (0x0C)
    ModeCalls* Patrol();                                 // 005D0F60 (0x20)
    ModeCalls* Player();                                 // 005D3760 (0x24)
    ModeCalls* Fear(int on);                             // 005D2850 (0x2C)
    ModeCalls* Aggression();                             // 004BA710 (0x18)
    ModeCalls* Revenge(int on);                          // 004BA830 (0x18)
    ModeCalls* Suspicion();                              // 004BA610 (0x18)
    ModeCalls* CorpseWatcher();                          // 004BA920 (0x08)
};
__declspec(naked) ModeCalls* ModeCalls::Sentry(const Vector3*) EI_ORIGINAL(0x004BA360)
__declspec(naked) ModeCalls* ModeCalls::Guard(const Vector3*, float, float) EI_ORIGINAL(0x004BA1C0)
__declspec(naked) ModeCalls* ModeCalls::Follow(CEngineObject*) EI_ORIGINAL(0x004BA450)
__declspec(naked) ModeCalls* ModeCalls::Patrol() EI_ORIGINAL(0x005D0F60)
ModeCalls* ModeCalls::Player() { return (ModeCalls*)static_cast<PlayerMode*>((void*)this)->Construct(); }
// Fear and Suspicion zero the mode's fields first (SV7b2, SV7d4: FearMode / SuspectionMode::ClearFields).
ModeCalls* ModeCalls::Fear(int on) {
    auto* mode = reinterpret_cast<FearMode*>(this);
    mode->ClearFields();
    return reinterpret_cast<ModeCalls*>(mode->Construct(static_cast<std::uint8_t>(on)));
}
__declspec(naked) ModeCalls* ModeCalls::Aggression() EI_ORIGINAL(0x004BA710)
__declspec(naked) ModeCalls* ModeCalls::Revenge(int) EI_ORIGINAL(0x004BA830)
ModeCalls* ModeCalls::Suspicion() {
    auto* mode = reinterpret_cast<SuspectionMode*>(this);
    mode->ClearFields();
    return reinterpret_cast<ModeCalls*>(mode->ScriptSuspectionModeConstruct());
}
__declspec(naked) ModeCalls* ModeCalls::CorpseWatcher() EI_ORIGINAL(0x004BA920)

// operator new for a mode: the command writes 0 to +4 (the reference count) before testing the
// pointer.
ModeCalls* NewMode(std::size_t size) {
    auto* p = static_cast<ModeCalls*>(gamecrt::operator_new(size));
    p->refCount = 0;
    return p;
}

struct PatrolCalls {
    void AddPoint(const Vector3* point);                 // 005D12B0
    PatrolPoint* LastPoint();                            // 005D1690
};
void PatrolCalls::AddPoint(const Vector3* a0) { static_cast<PatrolMode*>((void*)this)->AppendPoint(a0); }
__declspec(naked) PatrolPoint* PatrolCalls::LastPoint() EI_ORIGINAL(0x005D1690)

// A look (16 bytes: point and a flag) as the patrol commands build it.
struct PatrolLook {
    PatrolLook* Set(float x, float y, float z, int flag);   // 004BA1A0
    std::uint32_t data[4];
};
__declspec(naked) PatrolLook* PatrolLook::Set(float, float, float, int) EI_ORIGINAL(0x004BA1A0)

__declspec(naked) void __fastcall PatrolLooksSetAtGrow(AILookArray*, void*, int, PatrolLook*) EI_ORIGINAL(0x004CDCB0)

// The unit's server half (CUnit::controller).
struct ControllerCalls : ControllerOriginals {
    void PlayAnimation(const char* name);                // 00555700
    void SetMoveStyle(int style);                        // 00555270 (0 lie, 1 crawl, 2 walk, 3 run)
    void Reposition();                                   // 00554450
};
void ControllerCalls::PlayAnimation(const char* name) { ControllerPlayAnimationOriginal(this, nullptr, name); }
__declspec(naked) void ControllerCalls::SetMoveStyle(int) EI_ORIGINAL(0x00555270)
void ControllerCalls::Reposition() { ControllerRepositionOriginal((void*)this); }

struct UnitCalls {
    void SetHostileMask(std::uint32_t mask);             // 0052E970
    void SetController(WorldPlayer* player);             // 005300D0
    void GiveQuestItem(CEngineObject* item);             // 00531220
    void AddSpell(CEngineObject* spell);                 // 00531820
    void MarkChanged(int what);                          // 0050E9F0
};
void UnitCalls::SetHostileMask(std::uint32_t a0) { static_cast<CUnit*>((void*)this)->SetHostileMask(a0); }
void UnitCalls::SetController(WorldPlayer* player) { UnitSetControllerOriginal(this, nullptr, player); }
void UnitCalls::GiveQuestItem(CEngineObject* item) { UnitAddCarriedOriginal(this, nullptr, item); }
void UnitCalls::AddSpell(CEngineObject* spell) { UnitAddSpellOriginal(this, nullptr, spell); }
void UnitCalls::MarkChanged(int what) { reinterpret_cast<CMapObject*>(this)->MarkChanged(what); }
UnitCalls* UnitOf(CEngineObject* unit) { return reinterpret_cast<UnitCalls*>(unit); }

struct LeverCalls {
    void SwitchState(int state, float time);             // 0052B7A0
    void Enable(int on);                                 // 0052BAD0
    void SetScience(const void* info);                   // 0052BB20
};
__declspec(naked) void LeverCalls::SwitchState(int, float) EI_ORIGINAL(0x0052B7A0)
__declspec(naked) void LeverCalls::Enable(int) EI_ORIGINAL(0x0052BAD0)
__declspec(naked) void LeverCalls::SetScience(const void*) EI_ORIGINAL(0x0052BB20)

// A unit's statistics (CUnitServer::stats).
struct StatsCalls {
    void SetSkill(int index, int value, CUnit* owner);   // 00528AB0
    void SetScience(int index, int value, CUnit* owner); // 00528DA0
    void SetStrength(int value, CUnit* owner);           // 00529090
    void SetDexterity(int value, CUnit* owner);          // 00529220
    void SetIntelligence(int value, CUnit* owner);       // 00529400
};
__declspec(naked) void StatsCalls::SetSkill(int, int, CUnit*) EI_ORIGINAL(0x00528AB0)
__declspec(naked) void StatsCalls::SetScience(int, int, CUnit*) EI_ORIGINAL(0x00528DA0)
__declspec(naked) void StatsCalls::SetStrength(int, CUnit*) EI_ORIGINAL(0x00529090)
__declspec(naked) void StatsCalls::SetDexterity(int, CUnit*) EI_ORIGINAL(0x00529220)
__declspec(naked) void StatsCalls::SetIntelligence(int, CUnit*) EI_ORIGINAL(0x00529400)

// A player (WorldCalls::GetPlayer).
struct PlayerCalls {
    void GiveItems(void* list, int flag);                // 00665470
    void EraseQuestItem(int id);                         // 00665780
};
void PlayerCalls::GiveItems(void* a0, int a1) { PlayerGiveItemsOriginal(this, nullptr, a0, a1); }
void PlayerCalls::EraseQuestItem(int a0) { PlayerEraseQuestItemOriginal(this, nullptr, a0); }
PlayerCalls* PlayerOf(WorldPlayer* player) { return reinterpret_cast<PlayerCalls*>(player); }

// The player's server half (WorldPlayer::server): quest variables, music, zone change.
struct PlayerStateCalls {
    void SetVar(const char* name, float value) { PlayerStateSetVariableOriginal(this, nullptr, name, value); }          // 0066EE10
    void SetVarMax(const char* name, float value) { PlayerStateSetVariableMaxOriginal(this, nullptr, name, value); }    // 0066EE80
    void DelVar(const char* name);                       // 0066EF30
    void PlayMusic(const char* name);                    // 0066EB90
    void LeaveToZone(const char* zone, int point);       // 0066F310
    void PrepareRedeploy();                              // 0066EF80
};
__declspec(naked) void PlayerStateCalls::DelVar(const char*) EI_ORIGINAL(0x0066EF30)
__declspec(naked) void PlayerStateCalls::PlayMusic(const char*) EI_ORIGINAL(0x0066EB90)
__declspec(naked) void PlayerStateCalls::LeaveToZone(const char*, int) EI_ORIGINAL(0x0066F310)
void PlayerStateCalls::PrepareRedeploy() { static_cast<CPlayerServer*>((void*)this)->SendParty(); }

// Party records of a player (PartyRecordsFor) and one record.
struct PartyRecordsCalls {
    void AddUnit(const char* name, const char* prototype, const char* extra);   // 006628D0
    void RemoveUnit(int key);                            // 006620F0
};
void PartyRecordsCalls::AddUnit(const char* a0, const char* a1, const char* a2) { PartyAddMemberOriginal((void*)this, nullptr, a0, a1, a2); }
void PartyRecordsCalls::RemoveUnit(int a0) { PartyRemoveMemberOriginal(this, nullptr, static_cast<unsigned>(a0)); }
// 00660500: a record copies the items of `source` (with the records they belong to).
__declspec(naked) void __fastcall RecordCopyItemsFrom(PartyUnitRecord*, void*, PartyUnitRecord*, PartyRecordsCalls*) EI_ORIGINAL(0x00660500)

// The party manager.
struct PartyManagerCalls {
    void QuestCompleted();                               // 004A88E0
};
__declspec(naked) void PartyManagerCalls::QuestCompleted() EI_ORIGINAL(0x004A88E0)
PartyManagerCalls* ThePartyManager() { return reinterpret_cast<PartyManagerCalls*>(GamePartyManager()); }

// The named party records (party_records.h), through the originals: find 004A5E10 (a party),
// add 004A5E90, remove 004A5FA0.
__declspec(naked) CParty* __fastcall PartiesFind(NamedPartyRecords*, void*, const char*) EI_ORIGINAL(0x004A5E10)
NamedPartyRecords* Parties() { return &GamePartyManager()->records; }

// A party's items (00661DB0 add, 00661F30 remove).
int PartyAddItem(CParty* party, CEngineObject* item, int a, int b) { return (int)PartyAddItemOriginal(party, nullptr, item, a, b); }
void PartyRemoveItem(CParty* party, CEngineObject* item, int a) { PartyRemoveItemOriginal(party, nullptr, item, a); }

struct DiplomacyCalls {
    void SetRelation(int a, int b, int relation, int both);   // 004554E0
};
__declspec(naked) void DiplomacyCalls::SetRelation(int, int, int, int) EI_ORIGINAL(0x004554E0)
DiplomacyCalls* TheDiplomacy() { return reinterpret_cast<DiplomacyCalls*>(GameDiplomacy()); }

// The game flow: multiplayer state and quest checks.
struct FlowCalls {
    bool IsMultiplayer();                                // 0057F440
    bool QuestAllowed(const char* name);                 // 0057FB90
};
bool FlowCalls::IsMultiplayer() { return IsNetworkClientOriginal(); }
__declspec(naked) bool FlowCalls::QuestAllowed(const char*) EI_ORIGINAL(0x0057FB90)
FlowCalls* TheNetwork() { return reinterpret_cast<FlowCalls*>(TheGameFlow()); }

// STLport strings of the server's variable calls, built and destroyed with the original routines
// (two constructions are used, as in the original).
struct StlString {
    void InitEmpty(void* allocator);                     // 004217E0
    void AssignRange(const char* first, const char* last);   // 00428F10
    void Destroy();                                      // 00425040
    void* InitFrom(const char* text, void* allocator);    // 0040C5A0
    std::uint32_t data[4];
};
void StlString::InitEmpty(void* allocator) { StringInitWithAllocator(this, nullptr, allocator); }
void StlString::AssignRange(const char* first, const char* last) { StringAssignSpan(this, nullptr, first, last); }
void StlString::Destroy() { StringBufferFree(this); }
void* StlString::InitFrom(const char* text, void* alloc) { return ::StringInitFromChars(this, nullptr, text, alloc); }
struct StlAllocator {
    StlAllocator* Init();                                // 0041F770
    void Destroy();                                      // 0040C760
    std::uint8_t data[4];
};
__declspec(naked) StlAllocator* StlAllocator::Init() EI_ORIGINAL(0x0041F770)
__declspec(naked) void StlAllocator::Destroy() EI_ORIGINAL(0x0040C760)

// The multiplayer game server's variables.
struct ServerCalls {
    void SetVar(const StlString* name, float value);     // 00419730
    void SetVarMax(const StlString* name, float value);  // 004197D0
    void DelVar(const StlString* name);                  // 004198D0
};
__declspec(naked) void ServerCalls::SetVar(const StlString*, float) EI_ORIGINAL(0x00419730)
__declspec(naked) void ServerCalls::SetVarMax(const StlString*, float) EI_ORIGINAL(0x004197D0)
__declspec(naked) void ServerCalls::DelVar(const StlString*) EI_ORIGINAL(0x004198D0)
ServerCalls* ServerPointer() { return reinterpret_cast<ServerCalls*>(net::GameServer()); }
// 004B9FD0: the server exists and is not shutting down.
bool ServerActive() {
    net::ServerSession* server = net::GameServer();
    return server && !(server->flags7_ & CRefCounted::kDetached);
}

// The world's player list (CWorld::players_): the event dispatch.
struct EventCalls {
    void Send(int type, int value);                      // 0066C340
    void SendString(int type, const char* text);         // 0066C3C0
};
__declspec(naked) void EventCalls::Send(int, int) EI_ORIGINAL(0x0066C340)
__declspec(naked) void EventCalls::SendString(int, const char*) EI_ORIGINAL(0x0066C3C0)
EventCalls* TheEvents() { return reinterpret_cast<EventCalls*>(WorldServer()->players_); }

// The world's spell manager (CWorld::manager4C_).
struct SpellManagerCalls {
    CEngineObject* CreateSpellFromText(const char* text) {   // 00689030
        return reinterpret_cast<CEngineObject*>(
            reinterpret_cast<CSpellManager*>(this)->CreateSpellFromText(text));
    }
};
SpellManagerCalls* TheSpellManager() { return reinterpret_cast<SpellManagerCalls*>(WorldServer()->manager4C_); }

// A particle source's effect (ScriptParticleSource::object, a CEffectParticle).
struct ParticleCalls {
    void AttachTo(int objectId) { ParticleSetAttachedToOriginal(reinterpret_cast<CEngineObject*>(this), nullptr, objectId); }   // 00679280
    void SetSize(float size);                            // 006792B0
};
__declspec(naked) void ParticleCalls::SetSize(float) EI_ORIGINAL(0x006792B0)
ParticleCalls* ParticleOf(ScriptParticleSource* source) { return reinterpret_cast<ParticleCalls*>(source->object); }

// The console's ostream.
struct OstreamCalls {
    OstreamCalls* PutFloat(float value);                 // 004BA9D0 (sets +4 = 1, then << double)
    OstreamCalls* PutString(const char* text);           // 006FC31D
    OstreamCalls* Manipulate(void* manipulator);         // 00434FE0
};
__declspec(naked) OstreamCalls* OstreamCalls::PutFloat(float) EI_ORIGINAL(0x004BA9D0)
OstreamCalls* OstreamCalls::PutString(const char* text) { return static_cast<OstreamCalls*>(OstreamPutString(this, nullptr, text)); }
OstreamCalls* OstreamCalls::Manipulate(void* manipulator) { return static_cast<OstreamCalls*>(OstreamManipulate(this, nullptr, manipulator)); }
OstreamCalls* Console() { return reinterpret_cast<OstreamCalls*>(&console::Out()); }
// 004BA9F0: the manipulator that ends a console line.
void* EndLine() { return reinterpret_cast<void*>(0x004BA9F0); }

struct RendererCalls {
    void SetBackground(float r, float g, float b);       // 004BA120
};
__declspec(naked) void RendererCalls::SetBackground(float, float, float) EI_ORIGINAL(0x004BA120)

struct CameraCalls {
    int Play(const char* name, int flag);                // 00578820
};
__declspec(naked) int CameraCalls::Play(const char*, int) EI_ORIGINAL(0x00578820)

// MFC containers the commands keep on the stack, built and destroyed through the originals: the
// units found (CPtrList: constructor 004C96A0, destructor 004C96F0), the items given to a player
// (constructor 004C98C0, AddTail 004C98F0, destructor 00427830) and the party slots (CArray of
// 40-byte slots: constructor 004BB000, destructor 004BB020).
using PtrList = mfc::CList<void*>;
struct PartySlot {
    int unitId;
    std::uint8_t unused_04[36];
};
using PartyArray = mfc::CArray<PartySlot>;
__declspec(naked) PartyArray* __fastcall PartySlotsConstruct(PartyArray*) EI_ORIGINAL(0x004BB000)
__declspec(naked) void __fastcall PartySlotsDestruct(PartyArray*) EI_ORIGINAL(0x004BB020)
struct ObjectIterator : ObjectIteratorOriginals {   // ObjectListIterator through the original addresses
    CBaseManager* list;
    void* node;
};

// Damage description for KillUnit (vtable +0x7C; 0x34 bytes used, the original reserves 0x4C).
__declspec(naked) void __fastcall ResetDamageInfoAt(void*) EI_ORIGINAL(0x00522B40)

// ---- Small accessors the original calls out of line (004B9F20-004BAA30) -------------------------

// The unit's server half (CUnit::controller).
void* UnitState(CEngineObject* unit) { return static_cast<CUnit*>(unit)->controller; }
CUnitServer* Controller(void* state) { return static_cast<CUnitServer*>(state); }
ControllerCalls* ControllerOf(void* state) { return static_cast<ControllerCalls*>(state); }
BrainCalls* BrainOf(void* state) { return reinterpret_cast<BrainCalls*>(Controller(state)->Brain()); }
StatsCalls* StatsOf(void* state) { return reinterpret_cast<StatsCalls*>(&Controller(state)->stats); }
Vector3* Position(CEngineObject* object) { return &static_cast<CMapObject*>(object)->position; }
PlayerStateCalls* StateOf(WorldPlayer* player) { return reinterpret_cast<PlayerStateCalls*>(player->server); }

CEngineObject* Arg(const SSCCmd* cmd, int i) { return cmd->objectArgs.data[i]->object; }
const char* Text(const SSCCmd* cmd, int i) { return cmd->stringArgs.data[i]; }

// The unit's server half when argument 0 is a unit that has one, else null.
void* UnitStateArg(const SSCCmd* cmd) {
    CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
    return unit ? UnitState(unit) : nullptr;
}

// Finds the particle source with `id`.
ScriptParticleSource* FindParticle(CScript* script, int id) {
    for (auto* node = script->particles_.head; node; node = node->next)
        if (node->data.id == id) return &node->data;
    return nullptr;
}

// The quest objective commands' text argument when they take none: an empty CString.
void QuestObjectiveNoText(int id, float value) {
    mfc::CString text(game::EmptyText());
    ScriptQuestAddObjective(id, &text, value);
}

// Runs a For body in the same ScriptRun: the first four fields are saved, the body block run from
// its start, and the fields restored (the variables are shared).
void RunBody(ScriptRun* run, CScript* script, ScriptDecl* body) {
    ScriptDecl* block = run->block;
    int index = run->index;
    int ticks = run->ticks;
    int sleepStart = run->sleepStart;
    run->block = body;
    run->index = 0;
    run->ticks = 0;
    run->sleepStart = -1;
    ExecuteOrig(run, nullptr, script);
    run->block = block;
    run->index = index;
    run->ticks = ticks;
    run->sleepStart = sleepStart;
}

// Copies group element `i` into the loop counter (a global object variable).
void SetLoopCounter(const SSCCmd* cmd, int i) {
    ScriptObjectRef* element = &cmd->groupArgs.data[0]->data[i];
    ScriptObjectRef* counter = cmd->objectArgs.data[0];
    counter->Reset();
    counter->Construct(*element);
}

int& ForDepth(CScript* script) { return script->forDepth_; }

// The commands that need more than a few lines.

void SetCP(const SSCCmd* cmd, bool fast) {
    CEngineObject* object = Arg(cmd, 0);
    if (!object) return;
    const float* f = cmd->floatArgs.data;
    AIMapCalls* map = TheAIMap();
    if (fast) {
        map->SetUpdates(0);
        Vector3 point = {f[0], f[1], f[2]};
        map->Move(object, &point);
        if (object->GetClassId() == kClassUnit) ControllerOf(UnitState(object))->Reposition();
        map->SetUpdates(1);
        UnitOf(object)->MarkChanged(2);
        return;
    }
    float oldZ = Position(object)->z;
    Vector3 point = {f[0], f[1], f[2]};
    map->Move(object, &point);
    if (object->GetClassId() == kClassUnit) {
        ControllerOf(UnitState(object))->Reposition();
        if (!(FcomStatus(oldZ, Position(object)->z) & 0x40)) UnitOf(object)->MarkChanged(2);
        return;
    }
    // Other objects: the units around the new place are taken off the sector map, moved off cells
    // they cannot stay in, and put back.
    PtrList units;
    PtrListConstructOriginal(&units, nullptr, 10);
    float radius = static_cast<CMapObject*>(object)->radius + static_cast<CMapObject*>(object)->radius;
    map->FindUnitsInRadius(Position(object), &units, radius);
    for (PtrList::Node* pos = units.head; pos;) {
        auto* unit = static_cast<CEngineObject*>(pos->data);
        pos = pos->next;
        map->Remove(unit);
    }
    for (PtrList::Node* pos = units.head; pos;) {
        Vector3 free;
        auto* unit = static_cast<CEngineObject*>(pos->data);
        pos = pos->next;
        if (!map->CanLeaveCell(unit)) {
            free = *Position(unit);
            map->FindFreeCellNear(unit, Position(unit), &free);
            *Position(unit) = free;
            ControllerOf(UnitState(unit))->Reposition();
        }
        map->Add(unit);
    }
    UnitOf(object)->MarkChanged(2);
    PtrListDestruct4C96F0Original(&units);
}

void GiveSkill(const SSCCmd* cmd) {
    void* state = cmd->objectArgs.data[0]->GetUnitPerception();
    if (!state) return;
    // The skill names (0078B8FC..0078B8B0) in the order the original tests them; science goes to
    // SetScience, the others to SetSkill with their index.
    static const char* const kNames[] = {"science", "melee", "archery", "backstab", "elemental",
                                         "sense", "astral", "stealth", "awareness"};
    static const int kIndex[] = {0, 0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < 9; ++i) {
        const auto* name = reinterpret_cast<const unsigned char*>(kNames[i]);
        if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(Text(cmd, 0)), name) != 0) continue;
        CUnit* owner = Controller(state)->unit;
        int value = Ftol(cmd->floatArgs.data[0]);
        StatsCalls* stats = StatsOf(state);
        if (i == 0) stats->SetScience(kIndex[i], value, owner);
        else stats->SetSkill(kIndex[i], value, owner);
    }
}

void RedeployParty(const SSCCmd* cmd) {
    WorldPlayer* player = TheWorld()->GetPlayer(Ftol(cmd->floatArgs.data[0]));
    if (!player) return;
    PlayerStateCalls* state = StateOf(player);
    PartyArray slots;
    PartySlotsConstruct(&slots);
    TheWorld()->CollectParty(player, &slots);
    void* party = player->Party();
    PartyManagerBeginRedeploy(ThePartyManager(), nullptr, party);
    PartyManagerEndRedeploy(ThePartyManager(), nullptr, party);
    // One slot and one party unit: the slot gets the unit's id.
    const PartyMemberArray& members = player->Party()->members_;
    if (slots.size == 1 && members.size == 1) slots.data[0].unitId = static_cast<int>(members.data[0].unitId);
    state->PrepareRedeploy();
    player->server->lastPosition_.z = -1313.0f;
    TheWorld()->Redeploy(player, &slots, 0);
    PartySlotsDestruct(&slots);
}

void CopyLoot(const SSCCmd* cmd, bool replace) {
    if (!TheWorld()->GetPlayer(Ftol(cmd->floatArgs.data[0]))) return;
    CParty* from = PartiesFind(Parties(), nullptr, Text(cmd, 0));
    CParty* to = PartiesFind(Parties(), nullptr, Text(cmd, 1));
    if (!from || !to) return;
    if (replace) {
        while (to->items_.size != 0) PartyRemoveItem(to, static_cast<CEngineObject*>(to->items_.data[0]), 1);
    }
    for (int i = 0; i < from->items_.size; ++i)
        PartyAddItem(to, static_cast<CEngineObject*>(from->items_.data[i]), 1, 1);
    if (replace) to->Money() = from->Money();
    else to->Money() += from->Money();
}

void GSSetVarMultiplayer(const SSCCmd* cmd) {
    const char* name = Text(cmd, 0);
    StlAllocator allocator;
    StlString text;
    text.InitEmpty(&allocator);
    text.AssignRange(name, name + std::strlen(name));
    ServerPointer()->SetVar(&text, cmd->floatArgs.data[1]);
    text.Destroy();
}

void GSVarMultiplayer(const SSCCmd* cmd, bool setMax) {
    StlAllocator allocator;
    StlString text;
    text.InitFrom(Text(cmd, 0), allocator.Init());
    if (setMax) ServerPointer()->SetVarMax(&text, cmd->floatArgs.data[1]);
    else ServerPointer()->DelVar(&text);
    StlStringDestroy(&text);
    allocator.Destroy();
}

void PlayMovie(const SSCCmd* cmd) {
    mfc::CString path = *GameDirectoryOriginal(GameGlueObject()) + MoviesFolder();
    mfc::CString file = path + cmd->stringArgs.data[0];
    PlayMovieFile(file, 0, 1, 1, 1, 1, 1);
}

void CastSpell(const SSCCmd* cmd, bool atUnit) {
    CEngineObject* spell = TheSpellManager()->CreateSpellFromText(Text(cmd, 0));
    if (spell) ++spell->refCount_;
    if (spell) {
        const float* f = cmd->floatArgs.data;
        Vector3 from = {f[0], f[1], 0.0f};
        if (atUnit) {
            CEngineObject* target = Arg(cmd, 0);
            if (target) TheWorld()->CastSpell(spell, 0, &from, target, Position(target), 0);
        } else {
            Vector3 to = {f[2], f[3], 0.0f};
            TheWorld()->CastSpell(spell, 0, &from, nullptr, &to, 0);
        }
    }
    if (spell) spell->Release();
}

void UnitModes(const SSCCmd* cmd) {
    // UMAg: aggression set 0..3 replaces modes 3, 4, 5 and 7.
    void* state = UnitStateArg(cmd);
    if (!state) return;
    BrainCalls* brain = BrainOf(state);
    brain->RemoveMode(3);
    brain->RemoveMode(4);
    brain->RemoveMode(5);
    brain->RemoveMode(7);
    switch (Round(cmd->floatArgs.data[0])) {
    case 0:
        brain->AddMode(NewMode(0x2C)->Fear(1));
        brain->AddMode(NewMode(0x18)->Suspicion());
        brain->AddMode(NewMode(0x08)->CorpseWatcher());
        break;
    case 1:
        brain->AddMode(NewMode(0x2C)->Fear(0));
        brain->AddMode(NewMode(0x18)->Suspicion());
        brain->AddMode(NewMode(0x08)->CorpseWatcher());
        break;
    case 2:
        brain->AddMode(NewMode(0x18)->Revenge(1));
        brain->AddMode(NewMode(0x2C)->Fear(0));
        brain->AddMode(NewMode(0x18)->Suspicion());
        brain->AddMode(NewMode(0x08)->CorpseWatcher());
        break;
    case 3:
        brain->AddMode(NewMode(0x18)->Aggression());
        brain->AddMode(NewMode(0x2C)->Fear(0));
        brain->AddMode(NewMode(0x18)->Suspicion());
        brain->AddMode(NewMode(0x08)->CorpseWatcher());
        break;
    default:
        break;
    }
}

} // namespace

// ---- ScriptRun::Execute -------------------------------------------------------------------------

BOOL ScriptRun::Execute(CScript* script) {
    ++ticks;
    if (index >= block->commands.size) return 0;
    vars.RemoveDeletedObjects();
    int cont = 1;
    while (index < block->commands.size) {
        if (!cont) return 1;
        SSCCmd* cmd = &block->commands.data[index];
        cmd->SetParameters(&cmd->localBindings, &vars);
        cmd->SetParameters(&cmd->globalBindings, script->Globals());
        for (int i = 0; i < cmd->subCalls.size; ++i)
            CalcParametersOrig(cmd, nullptr, script, &vars, &cmd->subCalls.data[i]);
        cont = 1;
        bool done = false;
        if (scriptaddon::Enabled()) {   // Rebirth's AddNewScriptCommands (hook at 004B684E)
            scriptaddon::Call call{cmd, script, nullptr, &cont, this};
            done = scriptaddon::Dispatch(&call);
        }

        const float* f = cmd->floatArgs.data;
        switch (done ? -1 : cmd->id) {
        // ---- Flow -------------------------------------------------------------------------------
        case 0x07: {   // Sleep(ticks): counts this run's Execute calls from the first one
            if (sleepStart < 0) sleepStart = ticks;
            int n = Round(f[0]);
            if (ticks - sleepStart < n) cont = 0;
            else sleepStart = -1;
            break;
        }
        case 0x09:     // WaitSegment(ticks): until this run has been executed that often
            if (ticks < Round(f[0])) cont = 0;
            break;
        case 0xDE:     // SleepUntil(condition)
            if (ZeroOrNaN(f[0])) cont = 0;
            break;
        case 0xB9: {   // SleepUntilIdle(unit): while the brain has an action (+0x984)
            CEngineObject* unit = Arg(cmd, 0);
            if (unit && unit->GetClassId() == kClassUnit && Controller(UnitState(unit))->Brain()->action != 0) cont = 0;
            break;
        }
        case 0x08: {   // a declared script: start an instance with the call's arguments
            int which = Ftol(f[0]);
            auto* definition = &script->Scripts().data[which];
            InstanceList::Node* node = InstanceAddHead(&script->instances_, nullptr, definition);
            ScriptDefinition* instance = &node->data;
            instance->BindCall(cmd);
            instance->index = 0;   // the killed flag
            break;
        }
        case 0x79:     // KillScript
            if (!killFlag) game::FatalError(kKillScriptNotAllowed);
            *killFlag = 1;
            break;
        case kCmdFor: {   // For(counter, group)(body): the group's members from last to first
            ++ForDepth(script);
            for (int i = cmd->groupArgs.data[0]->size - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case kCmdForIf: {   // ForIf(counter, group, condition)(body)
            ++ForDepth(script);
            const ScriptSubCall* condition = nullptr;
            for (int i = 0; i < cmd->subCalls.size; ++i) {
                if (cmd->subCalls.data[i].type == kScriptFloat) {
                    condition = &cmd->subCalls.data[i];
                    break;
                }
            }
            for (int i = cmd->groupArgs.data[0]->size - 1; i >= 0; --i) {
                SetLoopCounter(cmd, i);
                if (condition) CalcParametersOrig(cmd, nullptr, script, &vars, condition);
                if (!ZeroOrNaN(cmd->floatArgs.data[0])) RunBody(this, script, cmd->calledScript);
            }
            --ForDepth(script);
            break;
        }
        case kCmdAssign: {   // global = value: float 0 variable index, float 1 type - 1
            ScriptVars* globals = script->Globals();
            switch (Ftol(f[1])) {
            case 0:
                std::memcpy(&globals->floats.data[Ftol(f[0])], &f[2], 4);
                break;
            case 1:
                globals->strings.data[Ftol(f[0])] = cmd->stringArgs.data[0];
                break;
            case 2: {
                ScriptObjectRef* value = cmd->objectArgs.data[0];
                ScriptObjectRef* slot = &globals->objects.data[Ftol(f[0])];
                slot->Reset();
                slot->Construct(*value);
                break;
            }
            case 3: {
                ScriptGroup* value = cmd->groupArgs.data[0];
                GroupCopy(&globals->groups.data[Ftol(f[0])], nullptr, value);
                break;
            }
            default:
                break;
            }
            break;
        }

        // ---- Groups -----------------------------------------------------------------------------
        case 0x22:   // AddObject(group, object)
            cmd->groupArgs.data[0]->Insert(Arg(cmd, 0));
            break;
        case 0x23: {   // RemoveObject(group, object)
            ScriptGroup* group = cmd->groupArgs.data[0];
            int at = group->IndexOf(*cmd->objectArgs.data[0]);
            if (at >= 0) GroupRemoveAt(group, nullptr, at, 1);
            break;
        }

        // ---- Lights, particles, effects (CScript helpers) ---------------------------------------
        case 0x0B: script->CreatePointLight(cmd); break;
        case 0x0C: script->MovePointLight(cmd); break;
        case 0x0D: script->DeletePointLight(cmd); break;
        case 0x0E: script->ScriptCreateParticleSource(cmd); break;
        case 0x10: script->ScriptMoveParticleSource(cmd); break;
        case 0x0F: script->ScriptCreateLightning(cmd); break;
        case 0x17: script->ScriptCreateFx(cmd); break;
        case 0x16:     // CreateFXSource
        case 0x18:     // CreateRandomizedFXSource
            script->ScriptCreateFxSource(cmd);
            break;
        case 0x11: {   // DeleteParticleSource(id)
            int id = Round(f[0]);
            for (ParticleList::Node* node = script->particles_.head; node;) {
                ParticleList::Node* current = node;
                node = node->next;
                if (current->data.id == id) {
                    ParticleRemoveAt(&script->particles_, nullptr, current);
                    break;
                }
            }
            break;
        }
        case 0x12: {   // AttachParticleSource(id, objectId)
            int objectId = Round(f[1]);
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) ParticleOf(entry)->AttachTo(objectId);
            break;
        }
        case 0xE7: {   // AttachParticles(id, object)
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            int objectId = object->id_;
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) ParticleOf(entry)->AttachTo(objectId);
            break;
        }
        case 0xBA: {   // SetParticleSourceSize(id, size)
            if (ScriptParticleSource* entry = FindParticle(script, Round(f[0]))) ParticleOf(entry)->SetSize(f[1]);
            break;
        }
        case 0x1A: {   // DeleteLightning(id): every lightning with the id (list +0x4C, head +0x50)
            for (LightningList::Node* node = script->lightning_.head; node;) {
                LightningList::Node* current = node;
                node = node->next;
                if (current->data.id == Ftol(f[0])) LightningRemoveAt(&script->lightning_, nullptr, current);
            }
            break;
        }
        case 0x19: {   // DeleteFXSource(id) (list +0x30, head +0x34; the id at node +0x0C)
            int id = Round(f[0]);
            for (FxSourceList::Node* node = script->fxSources_.head; node; node = node->next) {
                if (node->data.id == id) {
                    FxSourceRemoveAt(&script->fxSources_, nullptr, node);
                    break;
                }
            }
            break;
        }

        // ---- World ------------------------------------------------------------------------------
        case 0x39:   // SendEvent(type, value)
            TheEvents()->Send(Ftol(f[0]), Ftol(f[1]));
            break;
        case 0x3A:   // SendStringEvent(type, text)
            TheEvents()->SendString(Ftol(f[0]), Text(cmd, 0));
            break;
        case 0x97: {   // GodMode(player, on): player +0x108
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) player->godMode = static_cast<std::uint8_t>(Ftol(f[1]));
            break;
        }
        case 0x37: {   // SetDiplomacy(a, b, value): > 0 allied (0), 0 or NaN neutral (1), < 0 enemies (2)
            int status = FcomStatus(f[2], 0.0f);
            int relation;
            if (!(status & 0x41)) relation = 0;
            else relation = (status & 0x40) ? 1 : 2;
            TheDiplomacy()->SetRelation(Ftol(f[0]), Ftol(f[1]), relation, 1);
            break;
        }
        case 0x43: {   // InvokeAlarm(type, x, y)
            Vector3 point = {f[1], f[2], 0.0f};
            TheWorld()->InvokeAlarm(Ftol(f[0]), &point, 1);
            break;
        }
        case 0x88: {   // AddUnitToServer(name, prototype, ..., x, y, z)
            Vector3 point = {f[0], f[1], f[2]};
            TheWorld()->AddUnit(TextKeyFromName(Text(cmd, 0)), Text(cmd, 1), Text(cmd, 2), &point);
            break;
        }
        case 0x89:     // RemoveUnitFromServer(object)
        case 0x96: {   // RemoveObjectFromServer(object)
            CEngineObject* object = Arg(cmd, 0);
            if (object) TheWorld()->RemoveObject(object->id_);
            break;
        }
        case 0xD2:   // RecalcMercBriefings
            TheWorld()->RecalcMercBriefings();
            break;
        case 0x1E: {   // SetWind(x, y, z, strength)
            Vector3 direction = {f[0], f[1], f[2]};
            TheWorld()->SetWind(&direction, f[3]);
            break;
        }
        case 0x1F: {   // SetBackGroundColor(r, g, b): 0..255 scaled by 1/255 (0073C36C)
            const float scale = ColourScale();
            float b = f[2] * scale;
            float g = f[1] * scale;
            float r = f[0] * scale;
            static_cast<RendererCalls*>(Renderer())->SetBackground(r, g, b);
            break;
        }
        case 0xB6:   // RunWorldTime(time): the configured speed (int at 0079BE5C)
            TheWorld()->SetTimeSpeed(static_cast<float>(ConfiguredTimeSpeed()));
            TheWorld()->SetTime(f[0]);
            break;
        case 0xB7:   // FixWorldTime(time)
            TheWorld()->SetTimeSpeed(0.0f);
            TheWorld()->SetTime(f[0]);
            break;
        case 0xB8:   // SetWaterLevel(a, level, b)
            TheWorld()->SetWaterLevel(Round(f[0]), f[1], Round(f[2]));
            break;
        case 0xA0:   // AddMob(name)
            static_cast<CWorldServer*>(WorldServer())->LoadMobFile(Text(cmd, 0));
            break;
        case 0xA1:   // PlayCamera(name)
            static_cast<CameraCalls*>(ClientGlueObject())->Play(Text(cmd, 0), 0);
            break;
        case 0xC1:   // PlayMovie(name)
            PlayMovie(cmd);
            break;
        case 0x7D:   // ConsoleFloat(value)
            Console()->PutFloat(f[0])->Manipulate(EndLine());
            break;
        case 0x7E:   // ConsoleString(text)
            Console()->PutString(Text(cmd, 0))->Manipulate(EndLine());
            break;
        case 0xC3: {   // AddRectToArea(area, x1, y1, x2, y2)
            void* area = script->FindArea(Round(f[0]));
            AreaAddRect(area, nullptr, f[1], f[2], f[3], f[4]);
            break;
        }
        case 0xC4: {   // AddRoundToArea(area, x, y, radius)
            void* area = script->FindArea(Round(f[0]));
            AreaAddRound(area, nullptr, f[1], f[2], f[3]);
            break;
        }
        case 0xC6:   // DeleteArea(area)
            script->DeleteArea(Round(f[0]));
            break;
        case 0xC2: {   // HideObject(object, hide): flags_ bit 0x10000
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            if (ZeroOrNaN(f[0])) object->flags_ &= ~0x10000u;
            else object->flags_ |= 0x10000u;
            break;
        }
        case 0x95: {   // ActivateTrap(object, on): flags_ bit 0x80000
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            if (ZeroOrNaN(f[0])) object->flags_ &= ~0x80000u;
            else object->flags_ |= 0x80000u;
            break;
        }
        case 0x8E:   // SetCP(object, x, y, z)
            SetCP(cmd, false);
            break;
        case 0xAD:   // SetCPFast(object, x, y, z)
            SetCP(cmd, true);
            break;

        // ---- Levers -----------------------------------------------------------------------------
        case 0x91:     // SwitchLeverState(lever, state)
        case 0x9E: {   // SwitchLeverStateEx(lever, state, time)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            float time;
            if (cmd->id == 0x91) std::memcpy(&time, "\x00\x00\x20\xC1", 4);   // -10.0f
            else time = f[1];
            reinterpret_cast<LeverCalls*>(lever)->SwitchState(Ftol(f[0]), time);
            break;
        }
        case 0xCC: {   // EnableLever(lever, on)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            reinterpret_cast<LeverCalls*>(lever)->Enable(ZeroOrNaN(f[0]) ? 0 : 1);
            break;
        }
        case 0xAC: {   // SetScience(lever, a, b, four flags)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            struct {
                std::uint32_t flags;
                int a;
                int b;
            } info = {1, 0, 0};
            info.a = Ftol(f[0]);
            info.b = Ftol(f[1]);
            info.flags = 0;
            if (!ZeroOrNaN(f[2])) info.flags = 1;
            if (!ZeroOrNaN(f[3])) info.flags |= 2;
            if (!ZeroOrNaN(f[4])) info.flags |= 4;
            if (!ZeroOrNaN(f[5])) info.flags |= 8;
            reinterpret_cast<LeverCalls*>(lever)->SetScience(&info);
            break;
        }

        // ---- Units ------------------------------------------------------------------------------
        case 0x32: {   // BlockUnit(unit, on): flags_ bit 0x800000
            CEngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != kClassUnit) break;
            if (ZeroOrNaN(f[0])) unit->flags_ &= ~0x800000u;
            else unit->flags_ |= 0x800000u;
            break;
        }
        case 0x36: {   // SetPlayer(unit, player)
            CEngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != kClassUnit) break;
            UnitOf(unit)->SetHostileMask(reinterpret_cast<CDiplomacy*>(TheDiplomacy())->GetHostileMask(Ftol(f[0])));
            static_cast<CUnit*>(unit)->SetSide(Ftol(f[0]));
            break;
        }
        case 0x38: {   // SetEnemy(unit, player): adds the player to the hostile mask (+0x244)
            CEngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != kClassUnit) break;
            int player = Ftol(f[0]);
            std::uint32_t mask = (1u << (player & 31)) | static_cast<CUnit*>(unit)->relationsA;
            static_cast<CUnit*>(unit)->AddHostileMask(mask);
            break;
        }
        case 0x35: {   // KillUnit(object)
            CEngineObject* object = Arg(cmd, 0);
            alignas(4) std::uint8_t damage[0x4C];
            ResetDamageInfoAt(damage);
            if (object) ObjectSlots(object)->ReceiveHit(reinterpret_cast<DamageInfo*>(damage), nullptr, nullptr);
            break;
        }
        case 0x44: {   // InflictDamage(unit, amount)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            std::uint32_t damage[15] = {};
            damage[5] = 0x3F800000u;    // +0x14 1.0
            damage[7] = 0;              // +0x1C
            damage[8] = 0x42C80000u;    // +0x20 100.0
            std::memcpy(&damage[9], &f[0], 4);   // +0x24 amount
            damage[10] = 0x3F800000u;   // +0x28 1.0
            damage[11] = 1;             // +0x2C
            // TakeHit(hit, attacker, source): three arguments (the original pushes the first 0 early).
            ServerSlots(Controller(state))->TakeHitSlot(reinterpret_cast<DamageInfo*>(damage), nullptr, nullptr);
            break;
        }
        case 0x83: {   // AddUnitUnderControl(player, unit)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (player && unit) UnitOf(unit)->SetController(player);
            break;
        }
        case 0x84: {   // RemoveUnitFromControl(player, unit)
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (unit) UnitOf(unit)->SetController(nullptr);
            break;
        }
        case 0x42:   // SetSpellAggression(unit, value): reads the unit's +0x170 and does nothing
            break;
        case 0xAE: {   // RemoveQuestItem(unit, id): the first quest item (+0x4A0, count +0x4A4) with the id
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            int count = static_cast<CUnit*>(unit)->carriedCount;
            int id = Ftol(f[0]);
            for (int i = 0; i < count; ++i) {
                auto* item = static_cast<CItemQuest*>(static_cast<CUnit*>(unit)->carried[i]);
                if (static_cast<int>(item->questItemId_) == id) {
                    static_cast<CUnit*>(unit)->RemoveCarried(i);
                    break;
                }
            }
            break;
        }
        case 0xAF: {   // GiveUnitQuestItem(unit, item)
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            CEngineObject* item = TheWorld()->FindItem(Text(cmd, 0));
            if (item && item->GetClassId() == kClassQuestItem) UnitOf(unit)->GiveQuestItem(item);
            break;
        }
        case 0xB3: {   // GiveUnitSpell(unit, spell)
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            CEngineObject* spell = TheWorld()->FindSpell(Text(cmd, 0));
            if (!spell) break;
            UnitOf(unit)->AddSpell(spell);
            void* state = UnitState(unit);
            ServerSlots(Controller(state))->RefreshEquipment();
            break;
        }
        case 0xCD:   // GiveSkill(unit, skill, value)
            GiveSkill(cmd);
            break;
        case 0xCE:     // GiveStrength(unit, value)
        case 0xCF:     // GiveDexterity
        case 0xD0: {   // GiveIntelligence
            void* state = cmd->objectArgs.data[0]->GetUnitPerception();
            if (!state) break;
            CUnit* owner = Controller(state)->unit;
            int value = Ftol(f[0]);
            StatsCalls* stats = StatsOf(state);
            if (cmd->id == 0xCE) stats->SetStrength(value, owner);
            else if (cmd->id == 0xCF) stats->SetDexterity(value, owner);
            else stats->SetIntelligence(value, owner);
            break;
        }

        // ---- Unit orders (the brain at the unit's +0x240 object +0x980) -------------------------
        case 0x02: {   // MoveToPoint(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x03: {   // RotateTo(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            const Vector3* at = Position(Controller(state)->unit);
            Vector3 direction = {f[0] - at->x, f[1] - at->y, -at->z};
            BrainOf(state)->TurnTowards(&direction, 0);
            break;
        }
        case 0x04: {   // MoveToObject(unit, target)
            void* state = UnitStateArg(cmd);
            CEngineObject* target = Arg(cmd, 1);
            if (!state || !target) break;
            Vector3 point = {Position(target)->x, Position(target)->y, 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x3B: {   // Follow(unit, leader)
            void* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->Follow(Arg(cmd, 1));
            break;
        }
        case 0x3C: {   // Attack(unit, target)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            CEngineObject* target = Arg(cmd, 1);
            if (target) BrainOf(state)->AttackTarget(target);
            break;
        }
        case 0xE8: {   // ResetTarget(unit)
            void* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ResetTarget();
            break;
        }
        case 0x3D: {   // Cast(unit, x, y): at the ground height there
            void* state = UnitStateArg(cmd);
            if (!state) break;
            float x = f[0];
            float y = f[1];
            float z = TheWorld()->GridHeight(x, y);
            Vector3 point = {x, y, z};
            BrainOf(state)->CastAt(0, &point, 0);
            break;
        }
        case 0xA8:   // CastSpellPoint(spell, x, y, x2, y2)
            CastSpell(cmd, false);
            break;
        case 0xA9:   // CastSpellUnit(spell, x, y, target)
            CastSpell(cmd, true);
            break;
        case 0x34: {   // Idle(unit)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            BrainOf(state)->ClearModes();
            BrainOf(state)->StopAction();
            break;
        }
        case 0x33: {   // Guard(unit, x, y, radius)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Guard(&point, f[2]);
            break;
        }
        case 0x94: {   // Sentry(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Sentry(&point);
            break;
        }
        case 0x1B: {   // PlayAnimation(unit, name)
            void* state = UnitStateArg(cmd);
            if (state) ControllerOf(state)->PlayAnimation(Text(cmd, 0));
            break;
        }
        case 0xC9:     // Lie
        case 0x3E:     // Crawl
        case 0x3F:     // Stand
        case 0x40:     // Walk
        case 0x41: {   // Run
            void* state = UnitStateArg(cmd);
            if (!state) break;
            int style = cmd->id == 0xC9 ? 0 : cmd->id == 0x3E ? 1 : cmd->id == 0x41 ? 3 : 2;
            ControllerOf(state)->SetMoveStyle(style);
            break;
        }
        case 0xB0: {   // Rest(unit)
            void* state = UnitStateArg(cmd);
            if (state) ControllerOf(state)->StopQueued();
            break;
        }

        // ---- Unit modes -------------------------------------------------------------------------
        case 0x24: {   // UMClear
            void* state = UnitStateArg(cmd);
            if (state) BrainOf(state)->ClearModes();
            break;
        }
        case 0x25: {   // UMSentry(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            ModeCalls* mode = NewMode(0x14);
            BrainOf(state)->AddMode(mode ? mode->Sentry(&point) : nullptr);
            break;
        }
        case 0x26:     // UMGuard(unit, x, y, radius)
        case 0x27: {   // UMGuardEx(unit, x, y, radius, radius2)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vector3 point = {f[0], f[1], 0.0f};
            float radius = f[2];
            float radius2;
            if (cmd->id == 0x26) std::memcpy(&radius2, "\x00\x00\x80\xBF", 4);   // -1.0f
            else radius2 = f[3];
            ModeCalls* mode = NewMode(0x20);
            BrainOf(state)->AddMode(mode ? mode->Guard(&point, radius, radius2) : nullptr);
            break;
        }
        case 0xCA: {   // UMFollow(unit, leader)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            CEngineObject* leader = Arg(cmd, 1);
            ModeCalls* mode = NewMode(0x0C);
            BrainOf(state)->AddMode(mode ? mode->Follow(leader) : nullptr);
            break;
        }
        case 0x28: {   // UMPatrol
            void* state = UnitStateArg(cmd);
            if (!state) break;
            ModeCalls* mode = NewMode(0x20);
            BrainOf(state)->AddMode(mode ? mode->Patrol() : nullptr);
            break;
        }
        case 0x29: {   // UMPatrolClear
            void* state = UnitStateArg(cmd);
            if (!state) break;
            void* patrol = BrainOf(state)->FindPatrolMode();
            if (patrol) PatrolPointArraySetSizeOriginal(&static_cast<PatrolMode*>(patrol)->points, nullptr, 0, -1);
            break;
        }
        case 0x2A: {   // UMPatrolAddPoint(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* patrol = static_cast<PatrolCalls*>(BrainOf(state)->FindPatrolMode());
            if (!patrol) break;
            Vector3 point = {f[0], f[1], 0.0f};
            patrol->AddPoint(&point);
            break;
        }
        case 0x2B: {   // UMPatrolAddPointLook(unit, x, y, z): a look at the last point
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* patrol = static_cast<PatrolCalls*>(BrainOf(state)->FindPatrolMode());
            if (!patrol) break;
            PatrolPoint* last = patrol->LastPoint();
            if (!last) break;
            PatrolLook look;
            PatrolLook* value = look.Set(f[0], f[1], f[2], 0);
            PatrolLooksSetAtGrow(&last->looks, nullptr, last->looks.size, value);
            break;
        }
        case 0x31: {   // UMPlayer
            void* state = UnitStateArg(cmd);
            if (!state) break;
            BrainCalls* brain = BrainOf(state);
            brain->ClearModes();
            ModeCalls* mode = NewMode(0x24);
            brain->AddMode(mode ? mode->Player() : nullptr);
            brain->StopAction();
            break;
        }
        case 0xC8:   // UMAg(unit, level)
            UnitModes(cmd);
            break;
        case 0x2C: {   // UMStandard: aggression, suspicion, corpse watcher
            void* state = UnitStateArg(cmd);
            if (!state) break;
            BrainCalls* brain = BrainOf(state);
            ModeCalls* mode = NewMode(0x18);
            brain->AddMode(mode ? mode->Aggression() : nullptr);
            mode = NewMode(0x18);
            brain->AddMode(mode ? mode->Suspicion() : nullptr);
            mode = NewMode(0x08);
            brain->AddMode(mode ? mode->CorpseWatcher() : nullptr);
            break;
        }
        case 0x2D: {   // UMAggression
            void* state = UnitStateArg(cmd);
            if (!state) break;
            ModeCalls* mode = NewMode(0x18);
            BrainOf(state)->AddMode(mode ? mode->Aggression() : nullptr);
            break;
        }
        case 0x2E: {   // UMRevenge
            void* state = UnitStateArg(cmd);
            if (!state) break;
            ModeCalls* mode = NewMode(0x18);
            BrainOf(state)->AddMode(mode ? mode->Revenge(1) : nullptr);
            break;
        }
        case 0xC7: {   // UMFear(unit, on)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            int on = ZeroOrNaN(f[0]) ? 0 : 1;
            ModeCalls* mode = NewMode(0x2C);
            BrainOf(state)->AddMode(mode ? mode->Fear(on) : nullptr);
            break;
        }
        case 0x2F: {   // UMSuspection
            void* state = UnitStateArg(cmd);
            if (!state) break;
            ModeCalls* mode = NewMode(0x18);
            BrainOf(state)->AddMode(mode ? mode->Suspicion() : nullptr);
            break;
        }
        case 0x30: {   // UMCorpseWatcher
            void* state = UnitStateArg(cmd);
            if (!state) break;
            ModeCalls* mode = NewMode(0x08);
            BrainOf(state)->AddMode(mode ? mode->CorpseWatcher() : nullptr);
            break;
        }

        // ---- Players: variables, parties, items -------------------------------------------------
        case 0x7F: {   // GSSetVar(player, name, value)
            if (TheNetwork()->IsMultiplayer() && ServerActive()) {
                GSSetVarMultiplayer(cmd);
                break;
            }
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) StateOf(player)->SetVar(Text(cmd, 0), f[1]);
            break;
        }
        case 0x98:     // GSSetVarMax(player, name, value)
        case 0x82: {   // GSDelVar(player, name)
            bool setMax = cmd->id == 0x98;
            if (TheNetwork()->IsMultiplayer() && ServerActive()) {
                GSVarMultiplayer(cmd, setMax);
                break;
            }
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            if (setMax) StateOf(player)->SetVarMax(Text(cmd, 0), f[1]);
            else StateOf(player)->DelVar(Text(cmd, 0));
            break;
        }
        case 0x1D: {   // PlayMusic(player, name)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) StateOf(player)->PlayMusic(Text(cmd, 0));
            break;
        }
        case 0x8A: {   // LeaveToZone(player, zone, point)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) StateOf(player)->LeaveToZone(Text(cmd, 0), Ftol(f[1]));
            break;
        }
        case 0xD3:   // RedeployParty(player)
            RedeployParty(cmd);
            break;
        case 0x86:     // AddUnitToParty(player, party.unit, prototype)
        case 0x87: {   // RemoveUnitFromParty(player, party.unit)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CString name(cmd->stringArgs.data[0]);
            auto* records = static_cast<PartyRecordsCalls*>(PartyForScript(player, &name));
            if (records) {
                if (cmd->id == 0x86)
                    records->AddUnit(name, Text(cmd, 1), game::EmptyText());
                else
                    records->RemoveUnit(TextKeyFromName(name));
            }
            break;
        }
        case 0xD4:     // CopyItems(player, from, to)
        case 0x9D: {   // CopyStats(player, from, to)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CString from(cmd->stringArgs.data[0]);
            mfc::CString to(cmd->stringArgs.data[1]);
            auto* fromRecords = static_cast<PartyRecordsCalls*>(PartyForScript(player, &from));
            auto* toRecords = static_cast<PartyRecordsCalls*>(PartyForScript(player, &to));
            PartyUnitRecord* source = reinterpret_cast<PartyRecords*>(fromRecords)->Find(TextKeyFromName(from));
            PartyUnitRecord* target = reinterpret_cast<PartyRecords*>(toRecords)->Find(TextKeyFromName(to));
            if (source && target) {
                if (cmd->id == 0xD4) {
                    RecordCopyItemsFrom(target, nullptr, source, toRecords);
                } else {
                    std::memcpy(target->stats, source->stats, sizeof(target->stats));
                    target->texts[0] = source->texts[0];
                }
            }
            break;
        }
        case 0x9A: {   // CreateParty(player, name)
            if (TheWorld()->GetPlayer(Ftol(f[0]))) Parties()->Add(Text(cmd, 0));
            break;
        }
        case 0x9B: {   // RemoveParty(player, name)
            if (TheWorld()->GetPlayer(Ftol(f[0]))) Parties()->Remove(Text(cmd, 0));
            break;
        }
        case 0x9C: {   // SetCurrentParty(player, name): the party list's +0x0C
            if (TheWorld()->GetPlayer(Ftol(f[0])))
                Parties()->text0C = cmd->stringArgs.data[0];
            break;
        }
        case 0xE0:   // AddLoot(player, from, to)
            CopyLoot(cmd, false);
            break;
        case 0xCB:   // CopyLoot(player, from, to)
            CopyLoot(cmd, true);
            break;
        case 0xA7: {   // GiveMoney(player, amount): player +0xC0
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            PtrList items;
            ItemListConstructOriginal(&items, nullptr, 10);
            player->Party()->Money() += Ftol(f[1]);
            PlayerOf(player)->GiveItems(&items, 0);
            PtrListDestructOriginal(&items);
            break;
        }
        case 0x92:     // GiveQuestItem(player, item)
        case 0xA6: {   // GiveItem(player, item)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            PtrList items;
            ItemListConstructOriginal(&items, nullptr, 10);
            CEngineObject* item = TheWorld()->FindItem(Text(cmd, 0));
            if (item) {
                PtrListAddTailOriginal(&items, nullptr, item);
                PlayerOf(player)->GiveItems(&items, 0);
            }
            PtrListDestructOriginal(&items);
            break;
        }
        case 0x93: {   // EraseQuestItem(player, id)
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) PlayerOf(player)->EraseQuestItem(Ftol(f[1]));
            break;
        }
        case 0xDF: {   // FixItems: every object of the world's list (+0x54) copies +0x24 to +0x20
            auto* list = static_cast<CBaseManager*>(WorldServer()->itemManager_);
            ObjectIterator it;
            it.list = nullptr;
            for (CEngineObject* object = it.First(list); object; object = it.Next()) static_cast<CItem*>(object)->Repair();
            if (it.list) {   // destructor (004BB050)
                --it.list->refCount_;
                it.list = nullptr;
            }
            break;
        }

        // ---- Quests -----------------------------------------------------------------------------
        case 0x9F: {   // QuestComplete(player, quest)
            if (TheNetwork()->IsMultiplayer()) {
                if (TheNetwork()->QuestAllowed(Text(cmd, 0))) {
                    void* quest = TheQuestTable()->Find(Text(cmd, 0));
                    if (quest) TheWorld()->AwardKill(nullptr, std::bit_cast<std::uint32_t>(static_cast<QuestRecord*>(quest)->exp));
                }
                break;
            }
            WorldPlayer* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            void* quest = TheQuestTable()->Find(Text(cmd, 0));
            if (!quest) break;
            TheWorld()->AwardKill(player, std::bit_cast<std::uint32_t>(static_cast<QuestRecord*>(quest)->exp));
            ThePartyManager()->QuestCompleted();
            break;
        }
        case 0xD5:   // QStart(name)
            ScriptQuestStart(Text(cmd, 0));
            break;
        case 0xD6:   // QFinish
            ScriptQuestFinish(script);
            break;
        case 0xD7:   // QObjArea(area)
        case 0xDD:   // QObjGetItem(item)
            QuestObjectiveNoText(cmd->id, f[0]);
            break;
        case 0xD8:   // QObjSeeObject(name)
        case 0xD9:   // QObjSeeUnit
        case 0xDA:   // QObjKillUnit
        case 0xDB:   // QObjKillGroup
            ScriptQuestAddObjective(cmd->id, &cmd->stringArgs.data[0], 0.0f);
            break;
        case 0xDC:   // QObjUse(name, value)
            ScriptQuestAddObjective(cmd->id, &cmd->stringArgs.data[0], f[0]);
            break;

        default:   // functions, sections and commands without effect here
            break;
        }

        FreeTemporariesOrig(script);
        if (cont) ++index;
    }
    return 1;
}
EI_REPLACE_METHOD(0x004B6760, ScriptRun::Execute);
