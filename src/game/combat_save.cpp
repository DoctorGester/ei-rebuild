// CUnitServer's Load and Save slots (vtable 0073EF38 slots 9 and 10). See combat.h and
// docs/notes/combat.md ("Third round").
#include "combat.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include "originals_core.h"

namespace {
// Fields of CUnitServer that combat.h leaves unnamed (offsets from the controller).
constexpr int kAnimation = 0x24;        // an AnimationList (0x14 bytes) and a dword at +0x38
constexpr int kMotion = 0x3C;           // motion object (005B2430 / 005B2570)
constexpr int kPlan = 0xC4;             // the plan of the current order (00544EF0 / 00544F60)
constexpr int kAnimation2 = 0x148;      // a second AnimationList and its dword at +0x15C
constexpr int kAttackers = 0x91C;       // CList of attacker records (0x91C vtable, +0x920 head, +0x928 count)
constexpr int kVoice = 0xB10;

constexpr int kVersionPlanDirection = 0x48;   // plan +0x38 and the orders' +0x18 byte
constexpr int kVersionFixedMaximum = 0x50;    // older saves take stats +0x6FC from the server object
constexpr int kVersion180 = 0x44;
constexpr int kVersionUnused = 0x48;
constexpr int kVersion18E = 0x4A;
constexpr int kVersion8E4 = 0x4C;
constexpr int kVersion938 = 0x54;
constexpr int kVersionPendingHit = 0x69;
constexpr int kVersion93C = 0x73;
constexpr int kSavedConstant = 2;             // written after +0x180, read back and dropped

constexpr int kContextEffectManager = 0x38 / 4;     // ManagerContext() slot 14
inline void*& UnitManager() { return *reinterpret_cast<void**>(0x007AFFA0); }
void* const kUnitListContext = reinterpret_cast<void*>(0x007AFF60);   // passed to 0050E220

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}
void* Address(void* object, int offset) { return static_cast<char*>(object) + offset; }

template <typename T>
void WriteCopy(UnitStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

int IdOrNone(const CEngineObject* object) { return object ? object->id_ : -1; }

// 00556C60 / 00556C40 (two copies): assigns a counted reference, releasing the previous object.
void AssignCounted(CEngineObject*& slot, CEngineObject* object) {
    CEngineObject* previous = slot;
    slot = object;
    if (object) ++object->refCount_;
    if (previous) previous->Release();
}
} // namespace

namespace {
// Not rewritten (forwarders):
// 005582A0 / 00558420 (__fastcall): load / save an AnimationList (count, then 0xC-byte entries).
__declspec(naked) void __fastcall AnimationListLoad(void*, UnitStream*) EI_ORIGINAL(0x005582A0)
__declspec(naked) void __fastcall AnimationListSave(void*, UnitStream*) EI_ORIGINAL(0x00558420)
// 005B2430 / 005B2570: the motion object at +0x3C.
__declspec(naked) void __fastcall MotionLoad(void*, void*, UnitStream*) EI_ORIGINAL(0x005B2430)
__declspec(naked) void __fastcall MotionSave(void*, void*, UnitStream*) EI_ORIGINAL(0x005B2570)
// 00544EF0 / 00544F60: the plan's own AnimationList part (its fields +0x18.. follow here).
__declspec(naked) void __fastcall PlanLoad(void*, void*, UnitStream*) EI_ORIGINAL(0x00544EF0)
__declspec(naked) void __fastcall PlanSave(void*, void*, UnitStream*) EI_ORIGINAL(0x00544F60)
// 0050E220 / 0050E290: a counted reference by id (load resolves through `context`).
__declspec(naked) void __fastcall CountedRefLoad(void*, void*, UnitStream*, void*) EI_ORIGINAL(0x0050E220)
__declspec(naked) void __fastcall CountedRefSave(void*, void*, UnitStream*) EI_ORIGINAL(0x0050E290)
// 005287A0 / 00528800: UnitStats.
__declspec(naked) void __fastcall StatsLoad(void*, void*, UnitStream*) EI_ORIGINAL(0x005287A0)
__declspec(naked) void __fastcall StatsSave(void*, void*, UnitStream*) EI_ORIGINAL(0x00528800)
// 005D8310 / 005D8860: the AI brain at +0x980.
__declspec(naked) void __fastcall BrainLoad(void*, void*, UnitStream*) EI_ORIGINAL(0x005D8310)
__declspec(naked) void __fastcall BrainSave(void*, void*, UnitStream*) EI_ORIGINAL(0x005D8860)
// 005AEC40 / 005AEC80: the voice at +0xB10.
__declspec(naked) void __fastcall VoiceLoad(void*, void*, UnitStream*) EI_ORIGINAL(0x005AEC40)
__declspec(naked) void __fastcall VoiceSave(void*, void*, UnitStream*) EI_ORIGINAL(0x005AEC80)
// 005585F0 (__fastcall): loads the attackers list; 00545430 saves one of its records.
__declspec(naked) void __fastcall AttackersLoad(void*, UnitStream*) EI_ORIGINAL(0x005585F0)
__declspec(naked) void __fastcall AttackerSave(void*, void*, UnitStream*) EI_ORIGINAL(0x00545430)
// 00403F40 CBaseManager::FindObject (base_manager.cpp), through its game address.

// Resolves an id read from the stream in `manager` (-1: none).
CEngineObject* ReadObject(UnitStream* stream, void* manager) {
    int id;
    stream->Read(&id, 4);
    return id == -1 ? nullptr : ManagerFindObject(manager, nullptr, id);
}

void LoadOrder(UnitStream* stream, UnitAction* order) {
    stream->Read(&order->kind, 4);
    stream->Read(&order->source, 4);
    CountedRefLoad(&order->target, nullptr, stream, kUnitListContext);
    stream->Read(order->point, 0xC);
    stream->Read(&order->unknown_1C, 4);
    stream->Read(&order->unknown_20, 1);
    if (stream->version >= kVersionPlanDirection) stream->Read(&order->flag18, 1);
}

void SaveOrder(UnitStream* stream, UnitAction* order) {
    stream->Write(&order->kind, 4);
    stream->Write(&order->source, 4);
    CountedRefSave(&order->target, nullptr, stream);
    stream->Write(order->point, 0xC);
    WriteCopy(stream, order->unknown_1C);
    WriteCopy(stream, order->unknown_20);
    WriteCopy(stream, order->flag18);
}

// The plan object's fields after its list (plan + 0x18 ..).
void LoadPlanFields(UnitStream* stream, void* plan) {
    stream->Read(Address(plan, 0x18), 4);
    stream->Read(Address(plan, 0x1C), 4);
    stream->Read(Address(plan, 0x20), 4);
    stream->Read(Address(plan, 0x2C), 4);
    stream->Read(Address(plan, 0x24), 4);
    stream->Read(Address(plan, 0x28), 1);
    stream->Read(Address(plan, 0x30), 1);
    stream->Read(Address(plan, 0x34), 4);
    if (stream->version >= kVersionPlanDirection) stream->Read(Address(plan, 0x38), 0xC);
}

void SavePlanFields(UnitStream* stream, void* plan) {
    WriteCopy(stream, At<int>(plan, 0x18));
    WriteCopy(stream, At<int>(plan, 0x1C));
    WriteCopy(stream, At<int>(plan, 0x20));
    stream->Write(Address(plan, 0x2C), 4);
    WriteCopy(stream, At<int>(plan, 0x24));
    WriteCopy(stream, At<std::uint8_t>(plan, 0x28));
    WriteCopy(stream, At<std::uint8_t>(plan, 0x30));
    stream->Write(Address(plan, 0x34), 4);
    stream->Write(Address(plan, 0x38), 0xC);
}
} // namespace

void CUnitServer::LoadServer(UnitStream* stream) {
    At<int>(this, 0x918) = 0;
    stream->Read(&unknown_020, 1);
    stream->Read(&unknown_021, 1);
    AnimationListLoad(Address(this, kAnimation), stream);
    stream->Read(Address(this, kAnimation + 0x14), 4);
    MotionLoad(Address(this, kMotion), nullptr, stream);
    PlanLoad(Address(this, kPlan), nullptr, stream);
    LoadPlanFields(stream, Address(this, kPlan));
    stream->Read(&keepHitDirection, 1);
    for (int offset = 0x145; offset <= 0x147; ++offset) stream->Read(Address(this, offset), 1);
    AnimationListLoad(Address(this, kAnimation2), stream);
    stream->Read(Address(this, kAnimation2 + 0x14), 4);
    stream->Read(&hitDirection, 4);
    stream->Read(&state, 4);
    stream->Read(&moveMode, 8);
    stream->Read(&moveMode2, 8);
    stream->Read(Address(this, 0x184), 4);
    stream->Read(Address(this, 0x188), 4);
    stream->Read(Address(this, 0x178), 4);
    LoadOrder(stream, &action);
    LoadOrder(stream, &action2);
    StatsLoad(&stats, nullptr, stream);
    if (stream->version < kVersionFixedMaximum)
        At<int>(this, 0x8DC) = At<int>(At<void*>(unit, 0x170), 0x2C);
    for (int i = 0; i < 4; ++i) stream->Read(&moveFactors[i], 4);
    for (int offset = 0x908; offset <= 0x914; offset += 4) stream->Read(Address(this, offset), 4);
    stream->Read(&claimedHostile, 4);
    BrainLoad(brain, nullptr, stream);
    VoiceLoad(Address(this, kVoice), nullptr, stream);
    stream->Read(Address(this, 0x18C), 1);
    stream->Read(Address(this, 0x18D), 1);
    stream->Read(Address(this, 0x8F4), 4);

    void* effects = ei::VCall<void*>(ManagerContext(), kContextEffectManager);
    AssignCounted(effectObject, ReadObject(stream, effects));
    AttackersLoad(Address(this, kAttackers), stream);
    stream->Read(Address(this, 0x1D8), 1);
    void* units = UnitManager();
    AssignCounted(At<CEngineObject*>(this, 0x1DC), ReadObject(stream, units));

    if (stream->version < kVersion180) return;
    stream->Read(Address(this, 0x180), 4);
    if (stream->version < kVersionUnused) return;
    int unused;
    stream->Read(&unused, 4);
    if (stream->version < kVersion18E) return;
    stream->Read(Address(this, 0x18E), 1);
    if (stream->version < kVersion8E4) return;
    stream->Read(Address(this, 0x8E4), 0x10);
    if (stream->version < kVersion938) return;
    stream->Read(Address(this, 0x938), 4);
    if (stream->version < kVersionPendingHit) return;
    stream->Read(&pendingHit, 0x3C);
    if (stream->version < kVersion93C) return;
    WriteCopy(stream, At<int>(this, 0x93C));    // sic: written, not read
}
EI_REPLACE_METHOD(0x00555D10, CUnitServer::LoadServer);

void CUnitServer::SaveServer(UnitStream* stream) {
    WriteCopy(stream, unknown_020);
    WriteCopy(stream, unknown_021);
    AnimationListSave(Address(this, kAnimation), stream);
    WriteCopy(stream, At<int>(this, kAnimation + 0x14));
    MotionSave(Address(this, kMotion), nullptr, stream);
    PlanSave(Address(this, kPlan), nullptr, stream);
    SavePlanFields(stream, Address(this, kPlan));
    for (int offset = 0x144; offset <= 0x147; ++offset) WriteCopy(stream, At<std::uint8_t>(this, offset));
    AnimationListSave(Address(this, kAnimation2), stream);
    WriteCopy(stream, At<int>(this, kAnimation2 + 0x14));
    WriteCopy(stream, hitDirection);
    stream->Write(&state, 4);
    stream->Write(&moveMode, 8);
    stream->Write(&moveMode2, 8);
    WriteCopy(stream, At<int>(this, 0x184));
    WriteCopy(stream, At<int>(this, 0x188));
    WriteCopy(stream, At<int>(this, 0x178));
    SaveOrder(stream, &action);
    SaveOrder(stream, &action2);
    StatsSave(&stats, nullptr, stream);
    for (int i = 0; i < 4; ++i) WriteCopy(stream, moveFactors[i]);
    stream->Write(Address(this, 0x908), 4);
    WriteCopy(stream, At<int>(this, 0x90C));
    WriteCopy(stream, At<int>(this, 0x910));
    WriteCopy(stream, speedClass);
    WriteCopy(stream, claimedHostile);
    BrainSave(brain, nullptr, stream);
    VoiceSave(Address(this, kVoice), nullptr, stream);
    WriteCopy(stream, At<std::uint8_t>(this, 0x18C));
    WriteCopy(stream, At<std::uint8_t>(this, 0x18D));
    WriteCopy(stream, At<int>(this, 0x8F4));
    WriteCopy(stream, IdOrNone(effectObject));
    WriteCopy(stream, At<int>(this, kAttackers + 0xC));     // the list's count
    struct Node {
        Node* next;
        Node* prev;
        std::uint8_t record[1];   // the attacker record (saved by 00545430)
    };
    for (Node* node = At<Node*>(this, kAttackers + 4); node;) {
        Node* current = node;
        node = node->next;
        AttackerSave(current->record, nullptr, stream);
    }
    WriteCopy(stream, At<std::uint8_t>(this, 0x1D8));
    WriteCopy(stream, IdOrNone(At<CEngineObject*>(this, 0x1DC)));
    WriteCopy(stream, At<int>(this, 0x180));
    WriteCopy(stream, kSavedConstant);
    WriteCopy(stream, At<std::uint8_t>(this, 0x18E));
    stream->Write(Address(this, 0x8E4), 0x10);
    WriteCopy(stream, At<int>(this, 0x938));
    stream->Write(&pendingHit, 0x3C);
    WriteCopy(stream, At<int>(this, 0x93C));
}
EI_REPLACE_METHOD(0x005561C0, CUnitServer::SaveServer);
