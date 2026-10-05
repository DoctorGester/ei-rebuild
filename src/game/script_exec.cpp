// Script command execution: ScriptRun::Execute (004B6760), the switch over every world script
// command. See script.h and docs/notes/script.md ("Command execution").
//
// Rebirth patches Execute in memory (it adds commands), so this replacement installs only in the
// vanilla game. Every callee is reached through its original address (forwarders below): several
// are patched by Rebirth (CalcParameters, FatalError, 005935C0, Execute itself for For bodies), and
// the rest still run original code or their own replacement's verify wrapper.
#include "script.h"
#include "game_strings.h"
#include "script_addon.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>
#include <bit>

#pragma warning(disable : 4740) // inline assembly: no optimisation across it

ScriptCommandObserver* g_scriptCommandObserver = nullptr;

namespace {

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct Vec3 {
    float x, y, z;
};

// ---- x87 helpers --------------------------------------------------------------------------------

// The game's _ftol (006E853C): truncating conversion, low 32 bits of the 64-bit result.
__declspec(naked) int __cdecl Ftol(float) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// FCOMP against the constant 0.0 at 0073B858; true when C3 is set (equal or unordered), the test
// every command uses for "flag argument is zero".
bool ZeroOrNaN(float value) {
    int status;
    __asm {
        fld value
        fcomp dword ptr ds:[0x0073B858]
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return (status & 0x40) != 0;
}

// FCOM a, b: the status bits C0 (0x01, less or unordered) and C3 (0x40, equal or unordered).
int FcomStatus(float a, float b) {
    int status;
    __asm {
        fld a
        fcomp b
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return status;
}

int Round(float value) { return x87::fistp(value); }

// ---- Original code reached by the commands ------------------------------------------------------

constexpr int kClassUnit = 0x50;
constexpr int kClassLever = 0x5D;
constexpr int kClassQuestItem = 0x14;

// Script helpers (script.h types, original addresses).
__declspec(naked) void __fastcall SetParametersOrig(SSCCmd*, void*, const MfcArray<ScriptBinding>*, ScriptVars*)
    EI_ORIGINAL(0x004B3F60)
__declspec(naked) void __fastcall CalcParametersOrig(SSCCmd*, void*, CScript*, ScriptVars*, const ScriptSubCall*)
    EI_ORIGINAL(0x004B4030)
__declspec(naked) void __fastcall BindCallOrig(ScriptDefinition*, void*, const SSCCmd*) EI_ORIGINAL(0x004BB6A0)
// CArray<ScriptObjectRef> (groups): Copy 004C1290, RemoveAt 004C2200.
inline void __fastcall GroupCopy(ScriptGroup* a0, void*, const ScriptGroup* a2) { GroupAssignOriginal((void*)a0, nullptr, (const void*)a2); }
__declspec(naked) void __fastcall GroupRemoveAt(ScriptGroup*, void*, int, int) EI_ORIGINAL(0x004C2200)
// CList<ScriptDefinition> AddHead (CScript +0x140; despite the AddTail-like shape it links the new
// node before the head): returns the new node.
__declspec(naked) void* __fastcall InstanceAddHead(void*, void*, const ScriptDefinition*) EI_ORIGINAL(0x004C7C90)
// CList RemoveAt for the particle (+0x14), FX source (+0x30) and lightning (+0x4C) lists.
__declspec(naked) void __fastcall ParticleRemoveAt(void*, void*, void*) EI_ORIGINAL(0x004C6040)
__declspec(naked) void __fastcall FxSourceRemoveAt(void*, void*, void*) EI_ORIGINAL(0x004C6370)
__declspec(naked) void __fastcall LightningRemoveAt(void*, void*, void*) EI_ORIGINAL(0x004C6680)
// CScript helpers taking the command (point lights, particles, FX, lightning).
__declspec(naked) void __fastcall CreatePointLight(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BD6A0)
__declspec(naked) void __fastcall MovePointLight(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BD820)
__declspec(naked) void __fastcall DeletePointLight(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BD920)
__declspec(naked) void __fastcall CreateParticleSource(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BD9A0)
__declspec(naked) void __fastcall CreateLightning(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BCFF0)
__declspec(naked) void __fastcall MoveParticleSource(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BDBB0)
__declspec(naked) void __fastcall CreateFX(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BD260)
__declspec(naked) void __fastcall CreateFXSource(CScript*, void*, SSCCmd*) EI_ORIGINAL(0x004BD310)
__declspec(naked) void __fastcall DeleteArea(CScript*, void*, int) EI_ORIGINAL(0x004BCF30)
// Areas (FindArea's result): add a rectangle / a circle.
__declspec(naked) void __fastcall AreaAddRect(void*, void*, float, float, float, float) EI_ORIGINAL(0x004B1ED0)
__declspec(naked) void __fastcall AreaAddRound(void*, void*, float, float, float) EI_ORIGINAL(0x004B2070)
// Quest builder (globals at 007AA60C): QStart(name), QFinish(script), objective(id, text, value).
__declspec(naked) void __fastcall QuestStart(const char*) EI_ORIGINAL(0x004BAA40)
__declspec(naked) void __fastcall QuestFinish(CScript*) EI_ORIGINAL(0x004BAAB0)
__declspec(naked) void __fastcall QuestObjective(int, const mfc::CString*, float) EI_ORIGINAL(0x004BAE10)
// The party records of `player` for a "party name" string (ECX player, EDX string).
__declspec(naked) void* __fastcall PartyRecordsFor(void*, mfc::CString*) EI_ORIGINAL(0x004BAEC0)
// Object id of a name (ECX = name).
__declspec(naked) int __fastcall TextKeyFromName(const char*) EI_ORIGINAL(0x00461360)
// 005DB3F0: plays a movie file (ECX path, EDX 0, five flags).
// 00500960: the renderer (returns the object whose 004BA120 sets the background colour).
void* __cdecl Renderer() { return DisplayObject(); }

// The world (007AFF60).
struct World {
    void* GetPlayer(int index);                                          // 00594180
    float GridHeight(float x, float y);                                  // 00594220
    void InvokeAlarm(int type, const Vec3* point, int raise);            // 00593FC0
    void AddUnit(int key, const char* prototype, const char* name, const Vec3* point);   // 005910A0
    void RemoveObject(int id);                                           // 005935C0 (Rebirth patches it)
    void RecalcMercBriefings();                                          // 00591800
    void SetWind(const Vec3* direction, float strength);                 // 0058EE20
    void SetTimeSpeed(float speed);                                      // 0058EEE0
    void SetTime(float time);                                            // 0058EEA0
    void SetWaterLevel(int a, float level, int b);                       // 00593FA0
    void AddMob(const char* name);                                       // 00595AB0
    void* FindItem(const char* name);                                    // 00594AB0
    void* FindSpell(const char* name);                                   // 00594BC0
    void CollectParty(void* player, void* list);                         // 00590A40
    void Redeploy(void* player, void* list, int flag);                   // 00590700
    void AwardKill(void* player, int id);                                // 005911C0
    void CastSpell(void* spell, int a, const Vec3* from, CEngineObject* target, const void* to, int b);  // 0058E850
};
void* World::GetPlayer(int id) { return ::WorldGetPlayer(this, nullptr, id); }
__declspec(naked) float World::GridHeight(float, float) EI_ORIGINAL(0x00594220)
__declspec(naked) void World::InvokeAlarm(int, const Vec3*, int) EI_ORIGINAL(0x00593FC0)
__declspec(naked) void World::AddUnit(int, const char*, const char*, const Vec3*) EI_ORIGINAL(0x005910A0)
void World::RemoveObject(int id) { WorldRemoveObject(this, nullptr, id); }
__declspec(naked) void World::RecalcMercBriefings() EI_ORIGINAL(0x00591800)
__declspec(naked) void World::SetWind(const Vec3*, float) EI_ORIGINAL(0x0058EE20)
__declspec(naked) void World::SetTimeSpeed(float) EI_ORIGINAL(0x0058EEE0)
void World::SetTime(float a0) { WorldSetTimeOriginal((void*)this, nullptr, std::bit_cast<std::uint32_t>(a0)); }
__declspec(naked) void World::SetWaterLevel(int, float, int) EI_ORIGINAL(0x00593FA0)
__declspec(naked) void World::AddMob(const char*) EI_ORIGINAL(0x00595AB0)
__declspec(naked) void* World::FindItem(const char*) EI_ORIGINAL(0x00594AB0)
__declspec(naked) void* World::FindSpell(const char*) EI_ORIGINAL(0x00594BC0)
__declspec(naked) void World::CollectParty(void*, void*) EI_ORIGINAL(0x00590A40)
__declspec(naked) void World::Redeploy(void*, void*, int) EI_ORIGINAL(0x00590700)
__declspec(naked) void World::AwardKill(void*, int) EI_ORIGINAL(0x005911C0)
__declspec(naked) void World::CastSpell(void*, int, const Vec3*, CEngineObject*, const void*, int) EI_ORIGINAL(0x0058E850)
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

// The AI map (007B00D0); its sector map is at +0x124.
struct AIMap {
    void Move(CEngineObject* object, const Vec3* point);                 // 004BA0E0
    void FindUnitsInRadius(const Vec3* point, void* list, float radius); // 004BA100
    void Remove(CEngineObject* object);                                  // 004BA0C0
    void Add(CEngineObject* object);                                     // 004BA0A0
    bool CanLeaveCell(CEngineObject* object);                            // 005BA830
    bool FindFreeCellNear(CEngineObject* object, const Vec3* from, Vec3* out);   // 005BA9D0
    char SetUpdates(int on);                                            // 004BA090 (byte +2)
};
__declspec(naked) void AIMap::Move(CEngineObject*, const Vec3*) EI_ORIGINAL(0x004BA0E0)
__declspec(naked) void AIMap::FindUnitsInRadius(const Vec3*, void*, float) EI_ORIGINAL(0x004BA100)
__declspec(naked) void AIMap::Remove(CEngineObject*) EI_ORIGINAL(0x004BA0C0)
__declspec(naked) void AIMap::Add(CEngineObject*) EI_ORIGINAL(0x004BA0A0)
__declspec(naked) bool AIMap::CanLeaveCell(CEngineObject*) EI_ORIGINAL(0x005BA830)
__declspec(naked) bool AIMap::FindFreeCellNear(CEngineObject*, const Vec3*, Vec3*) EI_ORIGINAL(0x005BA9D0)
__declspec(naked) char AIMap::SetUpdates(int) EI_ORIGINAL(0x004BA090)
AIMap* TheAIMap() { return reinterpret_cast<AIMap*>(0x007B00D0); }

// A unit's AI brain (the unit's +0x240 object, +0x980).
struct Brain {
    void Walk(const Vec3* point, int a, int b);          // 005D7AE0
    void TurnTowards(const Vec3* direction, int a);      // 005D7B30
    void Follow(CEngineObject* leader);                  // 005D7E40
    void AttackTarget(CEngineObject* target);            // 005D7B50
    void CastAt(int a, const Vec3* point, int b);        // 005D7DA0
    void ResetTarget();                                  // 005D7C30
    void ClearModes();                                   // 005D4DF0
    void StopAction();                                   // 005D7790
    void Guard(const Vec3* point, float radius);         // 005D6230
    void Sentry(const Vec3* point);                      // 005D64E0
    void AddMode(void* mode);                            // 005D5F60
    void* FindPatrolMode();                              // 005D5E30
    void RemoveMode(int type);                           // 005D6140
};
__declspec(naked) void Brain::Walk(const Vec3*, int, int) EI_ORIGINAL(0x005D7AE0)
__declspec(naked) void Brain::TurnTowards(const Vec3*, int) EI_ORIGINAL(0x005D7B30)
__declspec(naked) void Brain::Follow(CEngineObject*) EI_ORIGINAL(0x005D7E40)
__declspec(naked) void Brain::AttackTarget(CEngineObject*) EI_ORIGINAL(0x005D7B50)
__declspec(naked) void Brain::CastAt(int, const Vec3*, int) EI_ORIGINAL(0x005D7DA0)
__declspec(naked) void Brain::ResetTarget() EI_ORIGINAL(0x005D7C30)
__declspec(naked) void Brain::ClearModes() EI_ORIGINAL(0x005D4DF0)
__declspec(naked) void Brain::StopAction() EI_ORIGINAL(0x005D7790)
__declspec(naked) void Brain::Guard(const Vec3*, float) EI_ORIGINAL(0x005D6230)
__declspec(naked) void Brain::Sentry(const Vec3*) EI_ORIGINAL(0x005D64E0)
__declspec(naked) void Brain::AddMode(void*) EI_ORIGINAL(0x005D5F60)
__declspec(naked) void* Brain::FindPatrolMode() EI_ORIGINAL(0x005D5E30)
__declspec(naked) void Brain::RemoveMode(int) EI_ORIGINAL(0x005D6140)

// Unit modes (the brain's behaviour list). Each is allocated by the command and constructed here.
struct Mode {
    Mode* Sentry(const Vec3* point);                     // 004BA360 (0x14 bytes)
    Mode* Guard(const Vec3* point, float r1, float r2);  // 004BA1C0 (0x20)
    Mode* Follow(CEngineObject* leader);                 // 004BA450 (0x0C)
    Mode* Patrol();                                      // 005D0F60 (0x20)
    Mode* Player();                                      // 005D3760 (0x24)
    Mode* Fear(int on);                                 // 005D2850 (0x2C)
    Mode* Aggression();                                  // 004BA710 (0x18)
    Mode* Revenge(int on);                              // 004BA830 (0x18)
    Mode* Suspicion();                                   // 004BA610 (0x18)
    Mode* CorpseWatcher();                               // 004BA920 (0x08)
};
__declspec(naked) Mode* Mode::Sentry(const Vec3*) EI_ORIGINAL(0x004BA360)
__declspec(naked) Mode* Mode::Guard(const Vec3*, float, float) EI_ORIGINAL(0x004BA1C0)
__declspec(naked) Mode* Mode::Follow(CEngineObject*) EI_ORIGINAL(0x004BA450)
__declspec(naked) Mode* Mode::Patrol() EI_ORIGINAL(0x005D0F60)
__declspec(naked) Mode* Mode::Player() EI_ORIGINAL(0x005D3760)
__declspec(naked) Mode* Mode::Fear(int) EI_ORIGINAL(0x005D2850)
__declspec(naked) Mode* Mode::Aggression() EI_ORIGINAL(0x004BA710)
__declspec(naked) Mode* Mode::Revenge(int) EI_ORIGINAL(0x004BA830)
__declspec(naked) Mode* Mode::Suspicion() EI_ORIGINAL(0x004BA610)
__declspec(naked) Mode* Mode::CorpseWatcher() EI_ORIGINAL(0x004BA920)

// operator new for a mode: the command writes 0 to +4 (the reference count) before testing the
// pointer.
Mode* NewMode(std::size_t size) {
    auto* p = static_cast<Mode*>(gamecrt::operator_new(size));
    Field<int>(p, 4) = 0;
    return p;
}

struct PatrolMode {
    void AddPoint(const Vec3* point);                    // 005D12B0
    void* LastPoint();                                   // 005D1690
};
__declspec(naked) void PatrolMode::AddPoint(const Vec3*) EI_ORIGINAL(0x005D12B0)
__declspec(naked) void* PatrolMode::LastPoint() EI_ORIGINAL(0x005D1690)

// Patrol point (16 bytes: point and a flag) and the arrays the patrol commands touch.
struct PatrolLook {
    PatrolLook* Set(float x, float y, float z, int flag);   // 004BA1A0
    std::uint32_t data[4];
};
__declspec(naked) PatrolLook* PatrolLook::Set(float, float, float, int) EI_ORIGINAL(0x004BA1A0)
__declspec(naked) void __fastcall PatrolPointsClear(void*, void*, int, int) EI_ORIGINAL(0x004C9970)
__declspec(naked) void __fastcall PatrolLooksSetAtGrow(void*, void*, int, PatrolLook*) EI_ORIGINAL(0x004CDCB0)

// The unit's +0x240 object (server-side unit state).
struct UnitServer {
    void PlayAnimation(const char* name);                // 00555700
    void SetMoveStyle(int style);                        // 00555270 (0 lie, 1 crawl, 2 walk, 3 run)
    void StopQueued();                                   // 00555200
    void Reposition();                                   // 00554450
};
__declspec(naked) void UnitServer::PlayAnimation(const char*) EI_ORIGINAL(0x00555700)
__declspec(naked) void UnitServer::SetMoveStyle(int) EI_ORIGINAL(0x00555270)
__declspec(naked) void UnitServer::StopQueued() EI_ORIGINAL(0x00555200)
void UnitServer::Reposition() { ControllerRepositionOriginal((void*)this); }

struct Unit {
    void SetHostileMask(std::uint32_t mask);             // 0052E970
    void SetSide(int side);                              // 00530120
    void AddHostileMask(std::uint32_t mask);             // 0052E940
    void SetController(void* player);                    // 005300D0
    void GiveQuestItem(void* item);                      // 00531220
    void RemoveQuestItem(int index);                     // 005313B0
    void AddSpell(void* spell);                          // 00531820
    void MarkChanged(int what);                          // 0050E9F0
};
__declspec(naked) void Unit::SetHostileMask(std::uint32_t) EI_ORIGINAL(0x0052E970)
__declspec(naked) void Unit::SetSide(int) EI_ORIGINAL(0x00530120)
__declspec(naked) void Unit::AddHostileMask(std::uint32_t) EI_ORIGINAL(0x0052E940)
void Unit::SetController(void* player) { UnitSetControllerOriginal(this, nullptr, player); }
__declspec(naked) void Unit::GiveQuestItem(void*) EI_ORIGINAL(0x00531220)
__declspec(naked) void Unit::RemoveQuestItem(int) EI_ORIGINAL(0x005313B0)
__declspec(naked) void Unit::AddSpell(void*) EI_ORIGINAL(0x00531820)
void Unit::MarkChanged(int what) { UnitMarkChanged(this, nullptr, what); }

struct Lever {
    void SwitchState(int state, float time);             // 0052B7A0
    void Enable(int on);                                // 0052BAD0
    void SetScience(const void* info);                   // 0052BB20
};
__declspec(naked) void Lever::SwitchState(int, float) EI_ORIGINAL(0x0052B7A0)
__declspec(naked) void Lever::Enable(int) EI_ORIGINAL(0x0052BAD0)
__declspec(naked) void Lever::SetScience(const void*) EI_ORIGINAL(0x0052BB20)

// A unit's skills and stats (the +0x240 object's +0x1E0).
struct UnitStats {
    void SetSkill(int index, int value, void* owner);    // 00528AB0
    void SetScience(int index, int value, void* owner);  // 00528DA0
    void SetStrength(int value, void* owner);            // 00529090
    void SetDexterity(int value, void* owner);           // 00529220
    void SetIntelligence(int value, void* owner);        // 00529400
};
__declspec(naked) void UnitStats::SetSkill(int, int, void*) EI_ORIGINAL(0x00528AB0)
__declspec(naked) void UnitStats::SetScience(int, int, void*) EI_ORIGINAL(0x00528DA0)
__declspec(naked) void UnitStats::SetStrength(int, void*) EI_ORIGINAL(0x00529090)
__declspec(naked) void UnitStats::SetDexterity(int, void*) EI_ORIGINAL(0x00529220)
__declspec(naked) void UnitStats::SetIntelligence(int, void*) EI_ORIGINAL(0x00529400)

// A player (World::GetPlayer).
struct Player {
    void GiveItems(void* list, int flag);                // 00665470
    void EraseQuestItem(int id);                         // 00665780
};
__declspec(naked) void Player::GiveItems(void*, int) EI_ORIGINAL(0x00665470)
__declspec(naked) void Player::EraseQuestItem(int) EI_ORIGINAL(0x00665780)

// The player's +0x20 object: quest variables, music, zone change.
struct PlayerState {
    void SetVar(const char* name, float value);          // 0066EE10
    void SetVarMax(const char* name, float value);       // 0066EE80
    void DelVar(const char* name);                       // 0066EF30
    void PlayMusic(const char* name);                    // 0066EB90
    void LeaveToZone(const char* zone, int point);       // 0066F310
    void PrepareRedeploy();                              // 0066EF80
};
__declspec(naked) void PlayerState::SetVar(const char*, float) EI_ORIGINAL(0x0066EE10)
__declspec(naked) void PlayerState::SetVarMax(const char*, float) EI_ORIGINAL(0x0066EE80)
__declspec(naked) void PlayerState::DelVar(const char*) EI_ORIGINAL(0x0066EF30)
__declspec(naked) void PlayerState::PlayMusic(const char*) EI_ORIGINAL(0x0066EB90)
__declspec(naked) void PlayerState::LeaveToZone(const char*, int) EI_ORIGINAL(0x0066F310)
void PlayerState::PrepareRedeploy() { PlayerStateRefreshOriginal((void*)this); }

// Party records of a player (PartyRecordsFor) and one record.
struct PartyRecords {
    void* Find(int key);                                 // 006621B0
    void AddUnit(const char* name, const char* prototype, const char* extra);   // 006628D0
    void RemoveUnit(int key);                            // 006620F0
};
__declspec(naked) void* PartyRecords::Find(int) EI_ORIGINAL(0x006621B0)
void PartyRecords::AddUnit(const char* a0, const char* a1, const char* a2) { PartyAddMemberOriginal((void*)this, nullptr, a0, a1, a2); }
__declspec(naked) void PartyRecords::RemoveUnit(int) EI_ORIGINAL(0x006620F0)
struct PartyRecord {
    void CopyItemsFrom(void* source, void* records);     // 00660500
};
__declspec(naked) void PartyRecord::CopyItemsFrom(void*, void*) EI_ORIGINAL(0x00660500)

// The party manager (007AA5A0); its party list is at +0x24.
struct PartyManager {
    void BeginRedeploy(void* players);                   // 004A8890
    void EndRedeploy(void* players);                     // 004A8880
    void QuestCompleted();                               // 004A88E0
};
__declspec(naked) void PartyManager::BeginRedeploy(void*) EI_ORIGINAL(0x004A8890)
__declspec(naked) void PartyManager::EndRedeploy(void*) EI_ORIGINAL(0x004A8880)
__declspec(naked) void PartyManager::QuestCompleted() EI_ORIGINAL(0x004A88E0)
PartyManager* ThePartyManager() { return reinterpret_cast<PartyManager*>(0x007AA5A0); }

struct PartyList {
    void* Find(const char* name);                        // 004A5E10
    void Create(const char* name);                       // 004A5E90
    void Remove(const char* name);                       // 004A5FA0
};
__declspec(naked) void* PartyList::Find(const char*) EI_ORIGINAL(0x004A5E10)
__declspec(naked) void PartyList::Create(const char*) EI_ORIGINAL(0x004A5E90)
__declspec(naked) void PartyList::Remove(const char*) EI_ORIGINAL(0x004A5FA0)
PartyList* Parties() { return reinterpret_cast<PartyList*>(0x007AA5A0 + 0x24); }

// A party (PartyList::Find): +0x40 {items, count}, +0x50 money.
struct Party {
    int AddItem(void* item, int a, int b);              // 00661DB0
    void RemoveItem(void* item, int a);                  // 00661F30
};
int Party::AddItem(void* item, int a, int b) { return (int)PartyAddItemOriginal(this, nullptr, item, a, b); }
__declspec(naked) void Party::RemoveItem(void*, int) EI_ORIGINAL(0x00661F30)

struct Diplomacy {
    void SetRelation(int a, int b, int relation, int both);   // 004554E0
    std::uint32_t GetHostileMask(int player);                 // 00455660
};
__declspec(naked) void Diplomacy::SetRelation(int, int, int, int) EI_ORIGINAL(0x004554E0)
__declspec(naked) std::uint32_t Diplomacy::GetHostileMask(int) EI_ORIGINAL(0x00455660)
Diplomacy* TheDiplomacy() { return reinterpret_cast<Diplomacy*>(0x007C2BC4); }

// Network state (007AFE20) and the multiplayer game server (pointer at 0079B5DC).
struct Network {
    bool IsMultiplayer();                                // 0057F440
    bool QuestAllowed(const char* name);                 // 0057FB90
};
bool Network::IsMultiplayer() { return IsNetworkClientOriginal(); }
__declspec(naked) bool Network::QuestAllowed(const char*) EI_ORIGINAL(0x0057FB90)
Network* TheNetwork() { return reinterpret_cast<Network*>(0x007AFE20); }

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

struct GameServer {
    void SetVar(const StlString* name, float value);     // 00419730
    void SetVarMax(const StlString* name, float value);  // 004197D0
    void DelVar(const StlString* name);                  // 004198D0
};
__declspec(naked) void GameServer::SetVar(const StlString*, float) EI_ORIGINAL(0x00419730)
__declspec(naked) void GameServer::SetVarMax(const StlString*, float) EI_ORIGINAL(0x004197D0)
__declspec(naked) void GameServer::DelVar(const StlString*) EI_ORIGINAL(0x004198D0)
GameServer* ServerPointer() { return *reinterpret_cast<GameServer**>(0x0079B5DC); }
// 004B9FD0: the server exists and is not shutting down.
bool ServerActive(GameServer* server) {
    return server && !(Field<std::uint32_t>(server, 4) & 0x80000000u);
}

// Event dispatcher (pointer at 007AFF98).
struct Events {
    void Send(int type, int value);                      // 0066C340
    void SendString(int type, const char* text);         // 0066C3C0
};
__declspec(naked) void Events::Send(int, int) EI_ORIGINAL(0x0066C340)
__declspec(naked) void Events::SendString(int, const char*) EI_ORIGINAL(0x0066C3C0)
Events* TheEvents() { return *reinterpret_cast<Events**>(0x007AFF98); }

struct SpellManager {
    void* CreateSpellFromText(const char* text);         // 00689030
};
__declspec(naked) void* SpellManager::CreateSpellFromText(const char*) EI_ORIGINAL(0x00689030)
SpellManager* TheSpellManager() { return *reinterpret_cast<SpellManager**>(0x007AFFAC); }

// Particle source (the particle list element's first field).
struct ParticleSource {
    void AttachTo(int objectId);                         // 00679280
    void SetSize(float size);                            // 006792B0
};
__declspec(naked) void ParticleSource::AttachTo(int) EI_ORIGINAL(0x00679280)
__declspec(naked) void ParticleSource::SetSize(float) EI_ORIGINAL(0x006792B0)

// The console's ostream (007C55A8).
struct Ostream {
    Ostream* PutFloat(float value);                      // 004BA9D0 (sets +4 = 1, then << double)
    Ostream* PutString(const char* text);                // 006FC31D
    Ostream* Manipulate(void* manipulator);              // 00434FE0
};
__declspec(naked) Ostream* Ostream::PutFloat(float) EI_ORIGINAL(0x004BA9D0)
Ostream* Ostream::PutString(const char* text) { return static_cast<Ostream*>(OstreamPutString(this, nullptr, text)); }
Ostream* Ostream::Manipulate(void* manipulator) { return static_cast<Ostream*>(OstreamManipulate(this, nullptr, manipulator)); }
Ostream* Console() { return reinterpret_cast<Ostream*>(0x007C55A8); }
void* const kEndLine = reinterpret_cast<void*>(0x004BA9F0);

struct BackgroundColor {
    void Set(float r, float g, float b);                 // 004BA120
};
__declspec(naked) void BackgroundColor::Set(float, float, float) EI_ORIGINAL(0x004BA120)

struct CameraPlayer {
    int Play(const char* name, int flag);               // 00578820
};
__declspec(naked) int CameraPlayer::Play(const char*, int) EI_ORIGINAL(0x00578820)
struct MoviePaths {
    const mfc::CString* Directory();                     // 00578DC0
};
__declspec(naked) const mfc::CString* MoviePaths::Directory() EI_ORIGINAL(0x00578DC0)

// Quest kill table (007C87A8).
struct QuestTable {
    void* Find(const char* name);                        // 004C9C90
};
__declspec(naked) void* QuestTable::Find(const char*) EI_ORIGINAL(0x004C9C90)

// MFC-style containers the commands keep on the stack.
struct PtrList {      // CPtrList (0x1C)
    PtrList* Construct(int blockSize);                   // 004C96A0
    void Destruct();                                     // 004C96F0
    std::uint32_t data[7];
};
PtrList* PtrList::Construct(int a0) { return (PtrList*)PtrListConstructOriginal((void*)this, nullptr, a0); }
__declspec(naked) void PtrList::Destruct() EI_ORIGINAL(0x004C96F0)
struct ItemList {     // list of items given to a player (0x1C)
    ItemList* Construct(int blockSize);                  // 004C98C0
    void AddTail(void* item);                            // 004C98F0
    void Destruct();                                     // 00427830
    std::uint32_t data[7];
};
__declspec(naked) ItemList* ItemList::Construct(int) EI_ORIGINAL(0x004C98C0)
void ItemList::AddTail(void* a0) { PtrListAddTailOriginal((void*)this, nullptr, a0); }
void ItemList::Destruct() { PtrListDestructOriginal(this); }
struct PartyArray {   // CArray of 40-byte party slots (0x14)
    PartyArray* Construct();                             // 004BB000
    void Destruct();                                     // 004BB020
    void* vtable;
    char* data;
    int size;
    int maxSize;
    int growBy;
};
__declspec(naked) PartyArray* PartyArray::Construct() EI_ORIGINAL(0x004BB000)
__declspec(naked) void PartyArray::Destruct() EI_ORIGINAL(0x004BB020)
struct ObjectIterator {   // (list, node)
    void* First(void* list);                             // 00404920
    void* Next();                                        // 00404960
    void* list;
    void* node;
};
__declspec(naked) void* ObjectIterator::First(void*) EI_ORIGINAL(0x00404920)
__declspec(naked) void* ObjectIterator::Next() EI_ORIGINAL(0x00404960)

// Damage description for KillUnit (vtable +0x7C; 0x34 bytes used, the original reserves 0x4C).
__declspec(naked) void __fastcall ResetDamageInfo(void*) EI_ORIGINAL(0x00522B40)

// ---- Small accessors the original calls out of line (004B9F20-004BAA30) -------------------------

void* UnitState(CEngineObject* unit) { return Field<void*>(unit, 0x240); }
Brain* BrainOf(void* state) { return reinterpret_cast<Brain*>(static_cast<char*>(state) + 0x980); }
Vec3* Position(CEngineObject* object) { return &Field<Vec3>(object, 0x1C); }
PlayerState* StateOf(void* player) { return Field<PlayerState*>(player, 0x20); }

CEngineObject* Arg(const SSCCmd* cmd, int i) { return cmd->objectArgs.data[i]->object; }
const char* Text(const SSCCmd* cmd, int i) { return cmd->stringArgs.data[i]; }

// The unit's +0x240 state when argument 0 is a unit that has one, else null.
void* UnitStateArg(const SSCCmd* cmd) {
    CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
    return unit ? UnitState(unit) : nullptr;
}

// CList nodes (next, prev, data...).
struct ListNode {
    ListNode* next;
    ListNode* prev;
};
// The particle source list element (node +8): source, id.
struct ParticleEntry {
    ParticleSource* source;
    int id;
};
// GetNext: the element of `*pos`, advancing it.
template <typename T>
T* GetNext(ListNode** pos) {
    ListNode* node = *pos;
    *pos = node->next;
    return reinterpret_cast<T*>(node + 1);
}
// Finds the particle source with `id` (list at CScript +0x14, head +0x18).
ParticleEntry* FindParticle(CScript* script, int id) {
    ListNode* pos = Field<ListNode*>(script, 0x18);
    while (pos) {
        ParticleEntry* entry = GetNext<ParticleEntry>(&pos);
        if (entry->id == id) return entry;
    }
    return nullptr;
}

// The quest objective commands' text argument when they take none: an empty CString.
void QuestObjectiveNoText(int id, float value) {
    mfc::CString text(game::EmptyText());
    QuestObjective(id, &text, value);
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

int& ForDepth(CScript* script) { return Field<int>(script, 0x1E8); }

// The commands that need more than a few lines.

void SetCP(const SSCCmd* cmd, bool fast) {
    CEngineObject* object = Arg(cmd, 0);
    if (!object) return;
    const float* f = cmd->floatArgs.data;
    AIMap* map = TheAIMap();
    if (fast) {
        map->SetUpdates(0);
        Vec3 point = {f[0], f[1], f[2]};
        map->Move(object, &point);
        if (object->GetClassId() == kClassUnit) static_cast<UnitServer*>(UnitState(object))->Reposition();
        map->SetUpdates(1);
        reinterpret_cast<Unit*>(object)->MarkChanged(2);
        return;
    }
    float oldZ = Position(object)->z;
    Vec3 point = {f[0], f[1], f[2]};
    map->Move(object, &point);
    if (object->GetClassId() == kClassUnit) {
        static_cast<UnitServer*>(UnitState(object))->Reposition();
        if (!(FcomStatus(oldZ, Position(object)->z) & 0x40)) reinterpret_cast<Unit*>(object)->MarkChanged(2);
        return;
    }
    // Other objects: the units around the new place are taken off the sector map, moved off cells
    // they cannot stay in, and put back.
    PtrList units;
    units.Construct(10);
    float radius = Field<float>(object, 0x134) + Field<float>(object, 0x134);
    map->FindUnitsInRadius(Position(object), &units, radius);
    ListNode* pos = reinterpret_cast<ListNode*>(units.data[1]);
    while (pos) map->Remove(*GetNext<CEngineObject*>(&pos));
    pos = reinterpret_cast<ListNode*>(units.data[1]);
    while (pos) {
        Vec3 free;
        CEngineObject* unit = *GetNext<CEngineObject*>(&pos);
        if (!map->CanLeaveCell(unit)) {
            free = *Position(unit);
            map->FindFreeCellNear(unit, Position(unit), &free);
            *Position(unit) = free;
            static_cast<UnitServer*>(UnitState(unit))->Reposition();
        }
        map->Add(unit);
    }
    reinterpret_cast<Unit*>(object)->MarkChanged(2);
    units.Destruct();
}

void GiveSkill(const SSCCmd* cmd) {
    void* state = cmd->objectArgs.data[0]->GetUnitPerception();
    if (!state) return;
    // The skill names at 0078B8FC.. in the order the original tests them; science goes to
    // SetScience, the others to SetSkill with their index.
    static const std::uintptr_t kNames[] = {0x0078B8FC, 0x0078B8F4, 0x0078B8EC, 0x0078B8E0, 0x0078B8D4,
                                            0x0078B8CC, 0x0078B8C4, 0x0078B8BC, 0x0078B8B0};
    static const int kIndex[] = {0, 0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < 9; ++i) {
        const auto* name = reinterpret_cast<const unsigned char*>(kNames[i]);
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(Text(cmd, 0)), name) != 0) continue;
        void* owner = Field<void*>(state, 0x1C);
        int value = Ftol(cmd->floatArgs.data[0]);
        auto* stats = reinterpret_cast<UnitStats*>(static_cast<char*>(state) + 0x1E0);
        if (i == 0) stats->SetScience(kIndex[i], value, owner);
        else stats->SetSkill(kIndex[i], value, owner);
    }
}

void RedeployParty(const SSCCmd* cmd) {
    void* player = TheWorld()->GetPlayer(Ftol(cmd->floatArgs.data[0]));
    if (!player) return;
    PlayerState* state = StateOf(player);
    PartyArray slots;
    slots.Construct();
    TheWorld()->CollectParty(player, &slots);
    void* players = static_cast<char*>(player) + 0x70;
    ThePartyManager()->BeginRedeploy(players);
    ThePartyManager()->EndRedeploy(players);
    // One slot and one party unit: the slot gets the unit's id.
    auto* units = reinterpret_cast<MfcArray<char>*>(static_cast<char*>(player) + 0x9C);
    if (slots.size == 1 && units->size == 1) {
        char* unit = units->data;    // element 0 (0x784 bytes each)
        *reinterpret_cast<int*>(slots.data) = *reinterpret_cast<int*>(unit + 0x1C);
    }
    state->PrepareRedeploy();
    Field<std::uint32_t>(state, 0x38 + 8) = 0xC4A42000u;   // -1313.0f
    TheWorld()->Redeploy(player, &slots, 0);
    slots.Destruct();
}

void CopyLoot(const SSCCmd* cmd, bool replace) {
    if (!TheWorld()->GetPlayer(Ftol(cmd->floatArgs.data[0]))) return;
    void* from = Parties()->Find(Text(cmd, 0));
    void* to = Parties()->Find(Text(cmd, 1));
    if (!from || !to) return;
    // Party items: {data, count} at +0x40.
    auto items = [](void* party) { return reinterpret_cast<void**>(static_cast<char*>(party) + 0x40); };
    auto count = [&](void* party) { return Field<int>(party, 0x44); };
    if (replace) {
        while (count(to) != 0) static_cast<Party*>(to)->RemoveItem(static_cast<void**>(*items(to))[0], 1);
    }
    for (int i = 0; i < count(from); ++i) static_cast<Party*>(to)->AddItem(static_cast<void**>(*items(from))[i], 1, 1);
    if (replace) Field<int>(to, 0x50) = Field<int>(from, 0x50);
    else Field<int>(to, 0x50) += Field<int>(from, 0x50);
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
    mfc::CString path = *reinterpret_cast<MoviePaths*>(0x007AFEA4)->Directory() +
                        reinterpret_cast<const char*>(0x00784A4C);   // "Movies\"
    mfc::CString file = path + cmd->stringArgs.data[0];
    PlayMovieFile(file, 0, 1, 1, 1, 1, 1);
}

void CastSpell(const SSCCmd* cmd, bool atUnit) {
    void* spell = TheSpellManager()->CreateSpellFromText(Text(cmd, 0));
    if (spell) ++Field<int>(spell, 4);
    if (spell) {
        const float* f = cmd->floatArgs.data;
        Vec3 from = {f[0], f[1], 0.0f};
        if (atUnit) {
            CEngineObject* target = Arg(cmd, 0);
            if (target) TheWorld()->CastSpell(spell, 0, &from, target, Position(target), 0);
        } else {
            Vec3 to = {f[2], f[3], 0.0f};
            TheWorld()->CastSpell(spell, 0, &from, nullptr, &to, 0);
        }
    }
    if (spell) ei::VCall<void>(spell, 4);   // Release
}

void UnitModes(const SSCCmd* cmd) {
    // UMAg: aggression set 0..3 replaces modes 3, 4, 5 and 7.
    void* state = UnitStateArg(cmd);
    if (!state) return;
    Brain* brain = BrainOf(state);
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
    RemoveDeletedOrig(&vars);
    int cont = 1;
    while (index < block->commands.size) {
        if (!cont) return 1;
        SSCCmd* cmd = &block->commands.data[index];
        SetParametersOrig(cmd, nullptr, &cmd->localBindings, &vars);
        SetParametersOrig(cmd, nullptr, &cmd->globalBindings, script->Globals());
        for (int i = 0; i < cmd->subCalls.size; ++i)
            CalcParametersOrig(cmd, nullptr, script, &vars, &cmd->subCalls.data[i]);
        cont = 1;
        if (g_scriptCommandObserver) g_scriptCommandObserver->before(this, script, cmd);
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
            if (unit && unit->GetClassId() == kClassUnit && Field<int>(UnitState(unit), 0x984) != 0) cont = 0;
            break;
        }
        case 0x08: {   // a declared script: start an instance with the call's arguments
            int which = Ftol(f[0]);
            auto* definition = &script->Scripts().data[which];
            void* node = InstanceAddHead(script->Field(0x140), nullptr, definition);
            auto* instance = reinterpret_cast<ScriptDefinition*>(static_cast<char*>(node) + 8);
            BindCallOrig(instance, nullptr, cmd);
            instance->index = 0;   // the killed flag
            break;
        }
        case 0x79:     // KillScript
            if (!killFlag) game::FatalError(reinterpret_cast<const char*>(0x0078B904));
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
        case 0x0B: CreatePointLight(script, nullptr, cmd); break;
        case 0x0C: MovePointLight(script, nullptr, cmd); break;
        case 0x0D: DeletePointLight(script, nullptr, cmd); break;
        case 0x0E: CreateParticleSource(script, nullptr, cmd); break;
        case 0x10: MoveParticleSource(script, nullptr, cmd); break;
        case 0x0F: CreateLightning(script, nullptr, cmd); break;
        case 0x17: CreateFX(script, nullptr, cmd); break;
        case 0x16:     // CreateFXSource
        case 0x18:     // CreateRandomizedFXSource
            CreateFXSource(script, nullptr, cmd);
            break;
        case 0x11: {   // DeleteParticleSource(id)
            int id = Round(f[0]);
            ListNode* pos = Field<ListNode*>(script, 0x18);
            while (pos) {
                ListNode* node = pos;
                if (GetNext<ParticleEntry>(&pos)->id == id) {
                    ParticleRemoveAt(script->Field(0x14), nullptr, node);
                    break;
                }
            }
            break;
        }
        case 0x12: {   // AttachParticleSource(id, objectId)
            int objectId = Round(f[1]);
            if (ParticleEntry* entry = FindParticle(script, Round(f[0]))) entry->source->AttachTo(objectId);
            break;
        }
        case 0xE7: {   // AttachParticles(id, object)
            CEngineObject* object = Arg(cmd, 0);
            if (!object) break;
            int objectId = object->id_;
            if (ParticleEntry* entry = FindParticle(script, Round(f[0]))) entry->source->AttachTo(objectId);
            break;
        }
        case 0xBA: {   // SetParticleSourceSize(id, size)
            if (ParticleEntry* entry = FindParticle(script, Round(f[0]))) entry->source->SetSize(f[1]);
            break;
        }
        case 0x1A: {   // DeleteLightning(id): every lightning with the id (list +0x4C, head +0x50)
            ListNode* pos = Field<ListNode*>(script, 0x50);
            while (pos) {
                ListNode* node = pos;
                ParticleEntry* entry = GetNext<ParticleEntry>(&pos);
                if (entry->id == Ftol(f[0])) LightningRemoveAt(script->Field(0x4C), nullptr, node);
            }
            break;
        }
        case 0x19: {   // DeleteFXSource(id) (list +0x30, head +0x34; the id at node +0x0C)
            int id = Round(f[0]);
            for (ListNode* node = Field<ListNode*>(script, 0x34); node; node = node->next) {
                if (Field<int>(node, 0x0C) == id) {
                    FxSourceRemoveAt(script->Field(0x30), nullptr, node);
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
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) Field<std::uint8_t>(player, 0x108) = static_cast<std::uint8_t>(Ftol(f[1]));
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
            Vec3 point = {f[1], f[2], 0.0f};
            TheWorld()->InvokeAlarm(Ftol(f[0]), &point, 1);
            break;
        }
        case 0x88: {   // AddUnitToServer(name, prototype, ..., x, y, z)
            Vec3 point = {f[0], f[1], f[2]};
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
            Vec3 direction = {f[0], f[1], f[2]};
            TheWorld()->SetWind(&direction, f[3]);
            break;
        }
        case 0x1F: {   // SetBackGroundColor(r, g, b): 0..255 scaled by 1/255 (0073C36C)
            const float scale = *reinterpret_cast<const float*>(0x0073C36C);
            float b = f[2] * scale;
            float g = f[1] * scale;
            float r = f[0] * scale;
            static_cast<BackgroundColor*>(Renderer())->Set(r, g, b);
            break;
        }
        case 0xB6:   // RunWorldTime(time): the configured speed (int at 0079BE5C)
            TheWorld()->SetTimeSpeed(static_cast<float>(*reinterpret_cast<const int*>(0x0079BE5C)));
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
            TheWorld()->AddMob(Text(cmd, 0));
            break;
        case 0xA1:   // PlayCamera(name)
            reinterpret_cast<CameraPlayer*>(0x007AFEA5)->Play(Text(cmd, 0), 0);
            break;
        case 0xC1:   // PlayMovie(name)
            PlayMovie(cmd);
            break;
        case 0x7D:   // ConsoleFloat(value)
            Console()->PutFloat(f[0])->Manipulate(kEndLine);
            break;
        case 0x7E:   // ConsoleString(text)
            Console()->PutString(Text(cmd, 0))->Manipulate(kEndLine);
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
            DeleteArea(script, nullptr, Round(f[0]));
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
            reinterpret_cast<Lever*>(lever)->SwitchState(Ftol(f[0]), time);
            break;
        }
        case 0xCC: {   // EnableLever(lever, on)
            CEngineObject* lever = Arg(cmd, 0);
            if (!lever || lever->GetClassId() != kClassLever) break;
            reinterpret_cast<Lever*>(lever)->Enable(ZeroOrNaN(f[0]) ? 0 : 1);
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
            reinterpret_cast<Lever*>(lever)->SetScience(&info);
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
            reinterpret_cast<Unit*>(unit)->SetHostileMask(TheDiplomacy()->GetHostileMask(Ftol(f[0])));
            reinterpret_cast<Unit*>(unit)->SetSide(Ftol(f[0]));
            break;
        }
        case 0x38: {   // SetEnemy(unit, player): adds the player to the hostile mask (+0x244)
            CEngineObject* unit = Arg(cmd, 0);
            if (!unit || unit->GetClassId() != kClassUnit) break;
            int player = Ftol(f[0]);
            std::uint32_t mask = (1u << (player & 31)) | Field<std::uint32_t>(unit, 0x244);
            reinterpret_cast<Unit*>(unit)->AddHostileMask(mask);
            break;
        }
        case 0x35: {   // KillUnit(object)
            CEngineObject* object = Arg(cmd, 0);
            alignas(4) std::uint8_t damage[0x4C];
            ResetDamageInfo(damage);
            if (object) ei::VCall<void>(object, 0x7C / 4, static_cast<void*>(damage), 0, 0);
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
            ei::VCall<void>(state, 0x50 / 4, static_cast<void*>(damage), 0, 0);
            break;
        }
        case 0x83: {   // AddUnitUnderControl(player, unit)
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (player && unit) reinterpret_cast<Unit*>(unit)->SetController(player);
            break;
        }
        case 0x84: {   // RemoveUnitFromControl(player, unit)
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (unit) reinterpret_cast<Unit*>(unit)->SetController(nullptr);
            break;
        }
        case 0x42:   // SetSpellAggression(unit, value): reads the unit's +0x170 and does nothing
            break;
        case 0xAE: {   // RemoveQuestItem(unit, id): the first quest item (+0x4A0, count +0x4A4) with the id
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            int count = Field<int>(unit, 0x4A4);
            int id = Ftol(f[0]);
            for (int i = 0; i < count; ++i) {
                void* item = Field<void**>(unit, 0x4A0)[i];
                if (Field<int>(item, 0x4C) == id) {
                    reinterpret_cast<Unit*>(unit)->RemoveQuestItem(i);
                    break;
                }
            }
            break;
        }
        case 0xAF: {   // GiveUnitQuestItem(unit, item)
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            auto* item = static_cast<CEngineObject*>(TheWorld()->FindItem(Text(cmd, 0)));
            if (item && item->GetClassId() == kClassQuestItem) reinterpret_cast<Unit*>(unit)->GiveQuestItem(item);
            break;
        }
        case 0xB3: {   // GiveUnitSpell(unit, spell)
            CEngineObject* unit = cmd->objectArgs.data[0]->AsUnit();
            if (!unit) break;
            void* spell = TheWorld()->FindSpell(Text(cmd, 0));
            if (!spell) break;
            reinterpret_cast<Unit*>(unit)->AddSpell(spell);
            void* state = UnitState(unit);
            ei::VCall<void>(state, 0x74 / 4);
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
            void* owner = Field<void*>(state, 0x1C);
            int value = Ftol(f[0]);
            auto* stats = reinterpret_cast<UnitStats*>(static_cast<char*>(state) + 0x1E0);
            if (cmd->id == 0xCE) stats->SetStrength(value, owner);
            else if (cmd->id == 0xCF) stats->SetDexterity(value, owner);
            else stats->SetIntelligence(value, owner);
            break;
        }

        // ---- Unit orders (the brain at the unit's +0x240 object +0x980) -------------------------
        case 0x02: {   // MoveToPoint(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vec3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Walk(&point, 0, 0);
            break;
        }
        case 0x03: {   // RotateTo(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            const Vec3* at = Position(Field<CEngineObject*>(state, 0x1C));
            Vec3 direction = {f[0] - at->x, f[1] - at->y, -at->z};
            BrainOf(state)->TurnTowards(&direction, 0);
            break;
        }
        case 0x04: {   // MoveToObject(unit, target)
            void* state = UnitStateArg(cmd);
            CEngineObject* target = Arg(cmd, 1);
            if (!state || !target) break;
            Vec3 point = {Position(target)->x, Position(target)->y, 0.0f};
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
            Vec3 point = {x, y, z};
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
            Vec3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Guard(&point, f[2]);
            break;
        }
        case 0x94: {   // Sentry(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vec3 point = {f[0], f[1], 0.0f};
            BrainOf(state)->Sentry(&point);
            break;
        }
        case 0x1B: {   // PlayAnimation(unit, name)
            void* state = UnitStateArg(cmd);
            if (state) static_cast<UnitServer*>(state)->PlayAnimation(Text(cmd, 0));
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
            static_cast<UnitServer*>(state)->SetMoveStyle(style);
            break;
        }
        case 0xB0: {   // Rest(unit)
            void* state = UnitStateArg(cmd);
            if (state) static_cast<UnitServer*>(state)->StopQueued();
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
            Vec3 point = {f[0], f[1], 0.0f};
            Mode* mode = NewMode(0x14);
            BrainOf(state)->AddMode(mode ? mode->Sentry(&point) : nullptr);
            break;
        }
        case 0x26:     // UMGuard(unit, x, y, radius)
        case 0x27: {   // UMGuardEx(unit, x, y, radius, radius2)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Vec3 point = {f[0], f[1], 0.0f};
            float radius = f[2];
            float radius2;
            if (cmd->id == 0x26) std::memcpy(&radius2, "\x00\x00\x80\xBF", 4);   // -1.0f
            else radius2 = f[3];
            Mode* mode = NewMode(0x20);
            BrainOf(state)->AddMode(mode ? mode->Guard(&point, radius, radius2) : nullptr);
            break;
        }
        case 0xCA: {   // UMFollow(unit, leader)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            CEngineObject* leader = Arg(cmd, 1);
            Mode* mode = NewMode(0x0C);
            BrainOf(state)->AddMode(mode ? mode->Follow(leader) : nullptr);
            break;
        }
        case 0x28: {   // UMPatrol
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Mode* mode = NewMode(0x20);
            BrainOf(state)->AddMode(mode ? mode->Patrol() : nullptr);
            break;
        }
        case 0x29: {   // UMPatrolClear
            void* state = UnitStateArg(cmd);
            if (!state) break;
            void* patrol = BrainOf(state)->FindPatrolMode();
            if (patrol) PatrolPointsClear(static_cast<char*>(patrol) + 8, nullptr, 0, -1);
            break;
        }
        case 0x2A: {   // UMPatrolAddPoint(unit, x, y)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* patrol = static_cast<PatrolMode*>(BrainOf(state)->FindPatrolMode());
            if (!patrol) break;
            Vec3 point = {f[0], f[1], 0.0f};
            patrol->AddPoint(&point);
            break;
        }
        case 0x2B: {   // UMPatrolAddPointLook(unit, x, y, z): a look at the last point
            void* state = UnitStateArg(cmd);
            if (!state) break;
            auto* patrol = static_cast<PatrolMode*>(BrainOf(state)->FindPatrolMode());
            if (!patrol) break;
            void* last = patrol->LastPoint();
            if (!last) break;
            PatrolLook look;
            PatrolLook* value = look.Set(f[0], f[1], f[2], 0);
            void* looks = static_cast<char*>(last) + 0x0C;
            PatrolLooksSetAtGrow(looks, nullptr, Field<int>(looks, 8), value);
            break;
        }
        case 0x31: {   // UMPlayer
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Brain* brain = BrainOf(state);
            brain->ClearModes();
            Mode* mode = NewMode(0x24);
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
            Brain* brain = BrainOf(state);
            Mode* mode = NewMode(0x18);
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
            Mode* mode = NewMode(0x18);
            BrainOf(state)->AddMode(mode ? mode->Aggression() : nullptr);
            break;
        }
        case 0x2E: {   // UMRevenge
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Mode* mode = NewMode(0x18);
            BrainOf(state)->AddMode(mode ? mode->Revenge(1) : nullptr);
            break;
        }
        case 0xC7: {   // UMFear(unit, on)
            void* state = UnitStateArg(cmd);
            if (!state) break;
            int on = ZeroOrNaN(f[0]) ? 0 : 1;
            Mode* mode = NewMode(0x2C);
            BrainOf(state)->AddMode(mode ? mode->Fear(on) : nullptr);
            break;
        }
        case 0x2F: {   // UMSuspection
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Mode* mode = NewMode(0x18);
            BrainOf(state)->AddMode(mode ? mode->Suspicion() : nullptr);
            break;
        }
        case 0x30: {   // UMCorpseWatcher
            void* state = UnitStateArg(cmd);
            if (!state) break;
            Mode* mode = NewMode(0x08);
            BrainOf(state)->AddMode(mode ? mode->CorpseWatcher() : nullptr);
            break;
        }

        // ---- Players: variables, parties, items -------------------------------------------------
        case 0x7F: {   // GSSetVar(player, name, value)
            GameServer* server = ServerPointer();
            if (TheNetwork()->IsMultiplayer() && server && !(Field<std::uint32_t>(server, 4) & 0x80000000u)) {
                GSSetVarMultiplayer(cmd);
                break;
            }
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) StateOf(player)->SetVar(Text(cmd, 0), f[1]);
            break;
        }
        case 0x98:     // GSSetVarMax(player, name, value)
        case 0x82: {   // GSDelVar(player, name)
            bool setMax = cmd->id == 0x98;
            if (TheNetwork()->IsMultiplayer() && ServerActive(ServerPointer())) {
                GSVarMultiplayer(cmd, setMax);
                break;
            }
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            if (setMax) StateOf(player)->SetVarMax(Text(cmd, 0), f[1]);
            else StateOf(player)->DelVar(Text(cmd, 0));
            break;
        }
        case 0x1D: {   // PlayMusic(player, name)
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) StateOf(player)->PlayMusic(Text(cmd, 0));
            break;
        }
        case 0x8A: {   // LeaveToZone(player, zone, point)
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) StateOf(player)->LeaveToZone(Text(cmd, 0), Ftol(f[1]));
            break;
        }
        case 0xD3:   // RedeployParty(player)
            RedeployParty(cmd);
            break;
        case 0x86:     // AddUnitToParty(player, party.unit, prototype)
        case 0x87: {   // RemoveUnitFromParty(player, party.unit)
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CString name(cmd->stringArgs.data[0]);
            auto* records = static_cast<PartyRecords*>(PartyRecordsFor(player, &name));
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
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            mfc::CString from(cmd->stringArgs.data[0]);
            mfc::CString to(cmd->stringArgs.data[1]);
            auto* fromRecords = static_cast<PartyRecords*>(PartyRecordsFor(player, &from));
            auto* toRecords = static_cast<PartyRecords*>(PartyRecordsFor(player, &to));
            void* source = fromRecords->Find(TextKeyFromName(from));
            void* target = toRecords->Find(TextKeyFromName(to));
            if (source && target) {
                if (cmd->id == 0xD4) {
                    static_cast<PartyRecord*>(target)->CopyItemsFrom(source, toRecords);
                } else {
                    std::memcpy(static_cast<char*>(target) + 0x20, static_cast<char*>(source) + 0x20, 0x1C1 * 4);
                    *static_cast<mfc::CString*>(target) = *static_cast<mfc::CString*>(source);
                }
            }
            break;
        }
        case 0x9A: {   // CreateParty(player, name)
            if (TheWorld()->GetPlayer(Ftol(f[0]))) Parties()->Create(Text(cmd, 0));
            break;
        }
        case 0x9B: {   // RemoveParty(player, name)
            if (TheWorld()->GetPlayer(Ftol(f[0]))) Parties()->Remove(Text(cmd, 0));
            break;
        }
        case 0x9C: {   // SetCurrentParty(player, name): the party list's +0x0C
            if (TheWorld()->GetPlayer(Ftol(f[0])))
                *reinterpret_cast<mfc::CString*>(reinterpret_cast<char*>(Parties()) + 0x0C) = cmd->stringArgs.data[0];
            break;
        }
        case 0xE0:   // AddLoot(player, from, to)
            CopyLoot(cmd, false);
            break;
        case 0xCB:   // CopyLoot(player, from, to)
            CopyLoot(cmd, true);
            break;
        case 0xA7: {   // GiveMoney(player, amount): player +0xC0
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            ItemList items;
            items.Construct(10);
            Field<int>(player, 0xC0) += Ftol(f[1]);
            static_cast<Player*>(player)->GiveItems(&items, 0);
            items.Destruct();
            break;
        }
        case 0x92:     // GiveQuestItem(player, item)
        case 0xA6: {   // GiveItem(player, item)
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            ItemList items;
            items.Construct(10);
            void* item = TheWorld()->FindItem(Text(cmd, 0));
            if (item) {
                items.AddTail(item);
                static_cast<Player*>(player)->GiveItems(&items, 0);
            }
            items.Destruct();
            break;
        }
        case 0x93: {   // EraseQuestItem(player, id)
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (player) static_cast<Player*>(player)->EraseQuestItem(Ftol(f[1]));
            break;
        }
        case 0xDF: {   // FixItems: every object of the world's list (+0x54) copies +0x24 to +0x20
            void* list = Field<void*>(TheWorld(), 0x54);
            ObjectIterator it;
            it.list = nullptr;
            for (void* object = it.First(list); object; object = it.Next())
                Field<std::uint32_t>(object, 0x20) = Field<std::uint32_t>(object, 0x24);
            if (it.list) {   // destructor (004BB050)
                --Field<int>(it.list, 4);
                it.list = nullptr;
            }
            break;
        }

        // ---- Quests -----------------------------------------------------------------------------
        case 0x9F: {   // QuestComplete(player, quest)
            if (TheNetwork()->IsMultiplayer()) {
                if (TheNetwork()->QuestAllowed(Text(cmd, 0))) {
                    void* quest = reinterpret_cast<QuestTable*>(0x007C87A8)->Find(Text(cmd, 0));
                    if (quest) TheWorld()->AwardKill(nullptr, Field<int>(quest, 0x0C));
                }
                break;
            }
            void* player = TheWorld()->GetPlayer(Ftol(f[0]));
            if (!player) break;
            void* quest = reinterpret_cast<QuestTable*>(0x007C87A8)->Find(Text(cmd, 0));
            if (!quest) break;
            TheWorld()->AwardKill(player, Field<int>(quest, 0x0C));
            ThePartyManager()->QuestCompleted();
            break;
        }
        case 0xD5:   // QStart(name)
            QuestStart(Text(cmd, 0));
            break;
        case 0xD6:   // QFinish
            QuestFinish(script);
            break;
        case 0xD7:   // QObjArea(area)
        case 0xDD:   // QObjGetItem(item)
            QuestObjectiveNoText(cmd->id, f[0]);
            break;
        case 0xD8:   // QObjSeeObject(name)
        case 0xD9:   // QObjSeeUnit
        case 0xDA:   // QObjKillUnit
        case 0xDB:   // QObjKillGroup
            QuestObjective(cmd->id, &cmd->stringArgs.data[0], 0.0f);
            break;
        case 0xDC:   // QObjUse(name, value)
            QuestObjective(cmd->id, &cmd->stringArgs.data[0], f[0]);
            break;

        default:   // functions, sections and commands without effect here
            break;
        }

        if (g_scriptCommandObserver) g_scriptCommandObserver->after(this, script, cmd, cont);
        FreeTemporariesOrig(script);
        if (cont) ++index;
    }
    return 1;
}
EI_REPLACE_METHOD(0x004B6760, ScriptRun::Execute);
EI_REBIRTH_IMPLEMENTS(0x004B6760, ei::rebirth::Patch::AddNewScriptCommands);
