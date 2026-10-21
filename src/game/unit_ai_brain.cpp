// The unit AI brain's mode list and state resets (005CB0F0, 005D4DF0-005D6140), and its save-game
// Load / Save (005D8310 / 005D8860).
#include "map_spec.h"
#include "unit_ai.h"

#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "logic_settings.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {
constexpr float kNearPointCleared = 100000.0f;

// 0073FED8: the initial call radius (10.0).
float InitialCallRadius() { return 10.0f /*0073FED8*/; }
// 0073FF00: a new fear mode's distance (copied as a dword).
std::uint32_t FearDistanceBits() { return 0x41200000u /*0073FF00*/; }
// The CPtrList the sector map fills (vtable 0073D454, blocks of 10).

} // namespace


namespace {

const Vector3* PositionOf(CUnit* unit) { return &unit->position; }

// A new mode with the counted-object header cleared (the original writes +4 before testing the
// allocation) and its vtable installed.
template <typename Mode>
Mode* NewMode(void* vtable) {
    auto* mode = static_cast<Mode*>(gamecrt::operator_new(sizeof(Mode)));
    if (mode) {
        mode->refs = 0;
        mode->refsHigh = 0;
        mode->vtable = vtable;
    }
    return mode;
}

// The mode list's elements are owning pointers (RefPtr): released with RefPtr::Destroy, added
// with RefPtr::Share (which may hand out a clone).
RefPtr* AsRefPtr(AIMode** element) { return reinterpret_cast<RefPtr*>(element); }


// The inlined CList RemoveAll of the mode list, each element released first.
void ReleaseAllModes(mfc::CTypedPtrList<AIMode>& modes) {
    for (auto* node = modes.head; node; node = node->next) AsRefPtr(&node->data)->Destroy();
    modes.RemoveAll();
}

// The inlined RemoveAt: unlink, release the element, return the node to the free list (the last
// node frees the blocks).
void RemoveModeNode(mfc::CTypedPtrList<AIMode>& modes, mfc::CTypedPtrList<AIMode>::Node* node) {
    if (node == modes.head) modes.head = node->next;
    else node->prev->next = node->next;
    if (node == modes.tail) modes.tail = node->prev;
    else node->next->prev = node->prev;
    AsRefPtr(&node->data)->Destroy();
    modes.FreeNode(node);
}

// The inlined release of a counted object pointer (slot 4) of the memory lists, then RemoveAll.
void ReleaseAllObjects(mfc::CTypedPtrList<CUnit>& list) {
    for (auto* node = list.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    list.RemoveAll();
}

// The mode in charge loses its reference (RefPtr::Assign(nullptr) / the inlined release).
void DropCurrent(AIMode*& current) {
    AIMode* was = current;
    current = nullptr;
    if (was) was->Release();
}
} // namespace

void AIBrain::AddMode(AIMode* mode) {
    int type = mode->Virtuals()->GetType();
    bool exclusive = mode->Virtuals()->Exclusive();
    for (auto* node = modes.head; node;) {
        auto* next = node->next;
        AIModeVirtuals* old = node->data->Virtuals();
        if (old->GetType() == type || (exclusive && old->Exclusive())) {
            if (current == node->data) DropCurrent(current);
            RemoveModeNode(modes, node);
            break;
        }
        node = next;
    }
    auto* node = modes.NewNode(modes.tail, nullptr);
    if (mode != node->data) {
        AsRefPtr(&node->data)->Destroy();
        AsRefPtr(&node->data)->Share(mode->Virtuals());
    }
    if (modes.tail) modes.tail->next = node;
    else modes.head = node;
    modes.tail = node;
}
EI_REWRITTEN(0x005D5F60, AIBrain::AddMode);

void AIBrain::RemoveModesOfType(int type) {
    for (auto* node = modes.head; node;) {
        auto* next = node->next;
        if (node->data->GetType() == type) RemoveModeNode(modes, node);
        node = next;
    }
}
EI_REWRITTEN(0x005D6140, AIBrain::RemoveModesOfType);

void AIBrain::ClearModes() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
}
EI_REWRITTEN(0x005D4DF0, AIBrain::ClearModes);

void AIBrain::Reset() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    memory.Reset();
    actionPoint = Vector3{0.0f, 0.0f, 0.0f};
    looks.SetSize(0, -1);
    if (target) {
        target->Release();
        target = nullptr;
    }
    if (seen) {
        seen->Release();
        seen = nullptr;
    }
}
EI_REWRITTEN(0x005D4E80, AIBrain::Reset);

void AIBrain::Init() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    memory.Reset();
    actionPoint = Vector3{0.0f, 0.0f, 0.0f};
    looks.SetSize(0, -1);
    if (target) target.Reset();
    if (seen) seen.Reset();
    owner = nullptr;
    alerted = 0;
    alarm = 0;
    alarmCountdown = 1;
    alarmSlot = 0;
    actionTime = kNoTime;
    memory.seesEnemy = 0;
    alarmRecords.SetSize(0, -1);
    callRadius = InitialCallRadius();
    position = Vector3{0.0f, 0.0f, 0.0f};
    alertMode = 0;
    alwaysUpdated = 0;
    standCooldown = 0;
    followTimer = 0;
    previousPosition = Vector3{0.0f, 0.0f, 0.0f};
}
EI_REWRITTEN(0x005D4F70, AIBrain::Init);

void AIMemory::Reset() {
    ReleaseAllObjects(nearby);
    ReleaseAllObjects(visible);
    ReleaseAllObjects(seenCorpses);
    ReleaseAllObjects(corpses);
    noticedX = 0.0f;
    noticedY = 0.0f;
    noticedLevel = 0;
    nearbyCountdown = 0;
    reports.SetSize(0, -1);
    calls.SetSize(0, -1);
    noticedKind = 0;
}

EI_REWRITTEN(0x005CB0F0, AIMemory::Reset);

// --- Standard mode sets ---------------------------------------------------------------------------

void AIBrain::AddGuardMode(const Vector3* point, float radius, float wander) {
    auto* mode = NewMode<GuardMode>(aimode::kGuardVtable);
    if (mode) mode->Set(point, radius, wander);
    AddMode(mode);
}
EI_REWRITTEN(0x005D6300, AIBrain::AddGuardMode);

void AIBrain::AddSuspectionMode() {
    auto* mode = NewMode<SuspectionMode>(aimode::kSuspectionVtable);
    if (mode) mode->ClearFields();   // SV7d4: not left as memory held them
    AddMode(mode);
}
EI_REWRITTEN(0x005D63B0, AIBrain::AddSuspectionMode);

void AIBrain::AddAggressionMode() {
    auto* mode = NewMode<AggressionMode>(aimode::kAggressionVtable);
    if (mode) {
        mode->enemy = nullptr;
        mode->chosen = nullptr;
        mode->enemyPart = -1;
        mode->fixed = 0;
    }
    AddMode(mode);
}
EI_REWRITTEN(0x005D63F0, AIBrain::AddAggressionMode);

void AIBrain::AddCorpseWatcherMode() { AddMode(NewMode<CorpseWatcherMode>(aimode::kCorpseWatcherVtable)); }
EI_REWRITTEN(0x005D6440, AIBrain::AddCorpseWatcherMode);

FearMode* FearMode::Construct(std::uint8_t newFlag) {
    vtable = aimode::kFearVtable;
    std::uint32_t bits = FearDistanceBits();
    std::memcpy(&distance, &bits, sizeof(bits));
    spread = 3.0f;
    shelter.x = -1313.0f;    // unset (0xC4A42000)
    fleePoint.z = 0.0f;
    flag = newFlag;
    return this;
}
EI_REPLACE_METHOD(0x005D2850, FearMode::Construct);

void AIBrain::AddFearMode(std::uint8_t flag) {
    auto* mode = NewMode<FearMode>(aimode::kFearVtable);
    if (mode) {
        mode->ClearFields();   // SV7b2: not left as memory held them
        mode->Construct(flag);    // inlined by the original
    }
    AddMode(mode);
}
EI_REWRITTEN(0x005D6480, AIBrain::AddFearMode);

void AIBrain::AddSentryMode(const Vector3* point) {
    auto* mode = NewMode<SentryMode>(aimode::kSentryVtable);
    if (mode) mode->point = *point;
    AddMode(mode);
}
EI_REWRITTEN(0x005D65B0, AIBrain::AddSentryMode);

namespace {
// The tail both standard sets share.
void AddAlertModes(AIBrain* brain) {
    brain->AddSuspectionMode();
    brain->AddAggressionMode();
    brain->AddCorpseWatcherMode();
    brain->AddFearMode(0);
    if (brain->target) {
        brain->target->Release();
        brain->target = nullptr;
    }
    brain->action = AIBrain::kActionNone;
}
} // namespace

void AIBrain::StandardGuard(const Vector3* point, float radius) {
    ClearModes();
    AddGuardMode(point, radius, -1.0f);
    AddAlertModes(this);
}
EI_REWRITTEN(0x005D6230, AIBrain::StandardGuard);

void AIBrain::StandardSentry(const Vector3* point) {
    ClearModes();
    AddSentryMode(point);
    AddAlertModes(this);
}
EI_REWRITTEN(0x005D64E0, AIBrain::StandardSentry);

// --- Calls for help -------------------------------------------------------------------------------

void AIBrain::CallAllies(const Vector3* point) {
    mfc::CTypedPtrList<CUnit> units{};
    units.vtable = mfc::impl::PtrListClass::Vtable();
    units.blockSize = 10;
    CUnit* self = owner->unit;
    TheServerWorld()->Sectors()->FindUnitsInRadius(PositionOf(self), &units, callRadius);
    for (auto* node = units.head; node;) {
        auto* other = node->data;
        node = node->next;
        if (other == self || !(GameDiplomacy()->allied_[self->side] & (1u << (other->side & 31)))) continue;
        mfc::CString message;
        AIBrain* ally = other->ServerController()->Brain();
        if (console::FilterMask() & 1) {
            double dz = self->position.z - other->position.z;
            double dy = self->position.y - other->position.y;
            double dx = self->position.x - other->position.x;
            message.Format("Unit %i call for help unit %i from %fm distance", self->id_, other->id_,
                           std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        }
        ally->memory.HearCall(point, 0x1A, message);
    }
    // The list's RemoveAll: no element destruction.
    units.RemoveAll();
}
EI_REWRITTEN(0x005D5990, AIBrain::CallAllies);

// --- Perception memory ----------------------------------------------------------------------------

namespace {
// The inlined RemoveAt of a memory list: unlink, release the element, free the node.
void RemoveObjectNode(mfc::CTypedPtrList<CUnit>& list, mfc::CTypedPtrList<CUnit>::Node* node) {
    if (node == list.head) list.head = node->next;
    else node->prev->next = node->next;
    if (node == list.tail) list.tail = node->prev;
    else node->next->prev = node->prev;
    if (node->data) node->data->Release();
    node->data = nullptr;
    list.FreeNode(node);
}

// Removes every element holding `object` (each time searching again from the head).
void RemoveAllOf(mfc::CTypedPtrList<CUnit>& list, CEngineObject* object) {
    while (auto* node = list.Find(object)) RemoveObjectNode(list, node);
}
} // namespace

float* __fastcall CopyPoint2(float* out, const float* point) {
    float x = point[0];
    float y = point[1];
    out[0] = x;
    out[1] = y;
    return out;
}
EI_REWRITTEN(0x005CA050, CopyPoint2);

int AIMemory::Notice(const Vector3* point, int level, int kind) {
    if (level <= noticedLevel) return 0;
    int changed = 1;
    if (static_cast<int>(static_cast<unsigned>(level) - static_cast<unsigned>(noticedLevel)) <= 100) {
        double dx = point->x - noticedX;
        double dy = point->y - noticedY;
        double z = point->z;
        // TEST AH,0x41: farther than the limit, NaN not.
        changed = z * z + dy * dy + dx * dx > 10.0 ? 1 : 0;
    }
    noticedLevel = level;
    noticedX = point->x;
    noticedY = point->y;
    noticedKind = kind;
    return changed;
}

EI_REWRITTEN(0x005CBBE0, AIMemory::Notice);

void AIMemory::Forget(CEngineObject* object) {
    AIRef held{nullptr};
    held.Assign(object);
    RemoveAllOf(nearby, held.object);
    RemoveAllOf(visible, held.object);
    RemoveAllOf(seenCorpses, held.object);
    RemoveAllOf(corpses, held.object);
    for (int i = 0; i < reports.size; ++i) {
        if (reports.data[i].object == object) reports.RemoveAt(i--, 1);
    }
    held.Reset();
}

EI_REWRITTEN(0x005CC020, AIMemory::Forget);


void AICallRecord::Destruct() { text.~CString(); }

AICallRecord& AICallRecord::operator=(const AICallRecord& other) {
    std::memcpy(static_cast<void*>(this), &other, offsetof(AICallRecord, text));
    text = other.text;
    return *this;
}

void AICallElements::Construct(AICallRecord* calls, int count) {
    std::memset(static_cast<void*>(calls), 0, sizeof(AICallRecord) * static_cast<unsigned>(count));
    for (int i = 0; i < count; ++i) new (&calls[i].text) mfc::CString();
}

void AICallElements::Destruct(AICallRecord* calls, int count) {
    for (int i = 0; i < count; ++i) calls[i].Destruct();
}

// 005CDD60 (RET 8): SetSize. 005CD880 (RET 4): Add, returning the new index.
EI_REWRITTEN(0x005CDD60, AIHeardCalls::SetSize);
EI_REWRITTEN(0x005CD880, AIHeardCalls::Add);
EI_REWRITTEN(0x00558D00, AICallRecord::Destruct);

void __stdcall AIReportElements::Construct(AIReport* reports, int count) {
    std::memset(static_cast<void*>(reports), 0, sizeof(AIReport) * static_cast<unsigned>(count));
    for (int i = 0; i < count; ++i) reports[i].object = nullptr;
}

void AIReportElements::Destruct(AIReport* reports, int count) {
    for (int i = 0; i < count; ++i) {
        if (CEngineObject* object = reports[i].object) object->Release();
        reports[i].object = nullptr;
    }
}

EI_REWRITTEN(0x005CD670, AIReports::SetSize);
EI_REWRITTEN(0x005CD800, AIReports::RemoveAt);
EI_REWRITTEN(0x00558D10, AIReportElements::Construct);
std::uintptr_t CUnitServer::AddHostileMask(std::uint32_t mask) { return unit->AddHostileMask(mask); }
EI_REPLACE_METHOD(0x00552CF0, CUnitServer::AddHostileMask);

void AIHeardCalls::Update(CUnit* unit, AIMemory* memory) {
    for (int i = 0; i < size; ++i) {
        AICallRecord& call = data[i];
        if (--call.kind >= 0) continue;
        if (call.hostileMask != 0) {
            console::GameOStream* log = &(*AiLog() << "Unit ");
            log = reinterpret_cast<console::GameOStream*>(reinterpret_cast<libcrt::Ostream*>(log)->PutInt(unit->id_));
            console::EndLine(*log << " hear delayed sound: " << call.text);
            unit->ServerController()->AddHostileMask(static_cast<std::uint32_t>(call.hostileMask));
        } else if (memory->noticedLevel < 900) {
            // The inlined Notice of level 900: logged when the level rises by more than 100 or the
            // point is more than sqrt(10) away (TEST AH,0x41: NaN is near).
            bool changed = true;
            if (900 - memory->noticedLevel <= 100) {
                float dx = call.point.x - memory->noticedX;
                float dy = call.point.y - memory->noticedY;
                float dz = call.point.z - 0.0f;
                changed = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) > 10.0f;
            }
            float copy[2];
            const float* point = CopyPoint2(copy, &call.point.x);
            memory->noticedX = point[0];
            memory->noticedLevel = 900;
            memory->noticedY = point[1];
            memory->noticedKind = -3;
            if (changed) console::EndLine(*AiLog() << "Delayed sound: " << call.text);
        }
        call.Destruct();
        int after = size - i - 1;
        if (after != 0) libcrt::Memmove(&data[i], &data[i + 1], static_cast<std::size_t>(after) * sizeof(AICallRecord));
        --size;
        --i;
    }
}
EI_REWRITTEN(0x005CC9D0, AIHeardCalls::Update);

void AIMemory::Update(CUnitServer* who) {
    for (int i = 0; i < reports.size; ++i) {
        AIReport* entry = &reports.data[i];
        if (--entry->what >= 0) continue;
        int after = reports.size - i - 1;
        // The element's destructor (00558CE0): release the object.
        if (entry->object) entry->object->Release();
        entry->object = nullptr;
        if (after != 0) libcrt::Memmove(entry, entry + 1, static_cast<std::size_t>(after) * sizeof(AIReport));
        --reports.size;
        --i;
    }
    calls.Update(who->unit, this);
    // The AI events are the world server's +0x2838 (007B2798).
    TheServerWorld()->AIEvents()->Update(who, this);   // 005CAA00
    noticedLevel += noticedKind;
}

EI_REWRITTEN(0x005CB700, AIMemory::Update);

int AIMemory::IsEnemy(CUnit* self, CEngineObject* other) {
    auto* unit = static_cast<CUnit*>(other);
    if (self->side == unit->side || self->player == unit->player) return 1;
    return visible.Find(other) ? 1 : 0;
}

EI_REWRITTEN(0x005CB7F0, AIMemory::IsEnemy);
void AIReports::Report(CEngineObject* object, int what) {
    if (!object) return;
    for (int i = 0; i < size; ++i) {
        if (data[i].object == object) {
            if (data[i].what <= what) data[i].what = what;
            return;
        }
    }
    ++object->refCount_;    // the temporary counted reference of the original
    int index = size;
    SetSize(index + 1, -1);
    AIReport& entry = data[index];
    if (entry.object != object) {
        if (entry.object) entry.object->Release();
        entry.object = nullptr;
        ++object->refCount_;
        entry.object = object;
    }
    entry.what = what;
    object->Release();
}
EI_REWRITTEN(0x005CC620, AIReports::Report);

bool AIReports::Contains(CEngineObject* object) const {
    for (int i = 0; i < size; ++i)
        if (data[i].object == object) return true;
    return false;
}
EI_REWRITTEN(0x005CC560, AIReports::Contains);

// --- Save games -----------------------------------------------------------------------------------

namespace {
// The original's Save functions copy each scalar into a stack slot before writing it.
template <typename T>
void WriteCopy(CStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// A new mode of `type` as Load constructs it before reading its data (the abstract bases'
// constructors are overwritten by the vtables; the fields Load reads stay uninitialised). Null for
// an unknown type (9, or above 10). The original writes +4 before testing the allocation.
AIMode* NewModeOfType(int type) {
    switch (type) {
    case kModeGuard: {
        auto* mode = NewMode<GuardMode>(aimode::kGuardVtable);
        mode->failures = 0;
        return mode;
    }
    case kModePatrol: {
        auto* mode = NewMode<PatrolMode>(aimode::kPatrolVtable);
        mode->Construct();
        return mode;
    }
    case kModeSentry:
        return NewMode<SentryMode>(aimode::kSentryVtable);
    case kModeSuspection:
        return NewMode<SuspectionMode>(aimode::kSuspectionVtable);
    case kModeAggression: {
        auto* mode = NewMode<AggressionMode>(aimode::kAggressionVtable);
        mode->enemy = nullptr;
        mode->chosen = nullptr;
        mode->enemyPart = -1;
        mode->fixed = 0;
        return mode;
    }
    case kModeCorpseWatcher:
        return NewMode<CorpseWatcherMode>(aimode::kCorpseWatcherVtable);
    case kModePlayer: {
        auto* mode = NewMode<PlayerMode>(aimode::kPlayerVtable);
        mode->Construct();
        return mode;
    }
    case kModeFear: {
        auto* mode = NewMode<FearMode>(aimode::kFearVtable);
        mode->Construct(0);
        return mode;
    }
    case kModeTamed: {
        auto* mode = NewMode<PlayerMode>(aimode::kPlayerVtable);
        mode->Construct();
        mode->vtable = aimode::kTamedVtable;
        return mode;
    }
    case kModeFollow: {
        auto* mode = NewMode<FollowMode>(aimode::kFollowVtable);
        mode->leader = nullptr;
        return mode;
    }
    default:
        return nullptr;
    }
}

void AssignCurrent(AIMode*& current, AIMode* mode) {
    reinterpret_cast<RefPtr*>(&current)->Assign(mode ? mode->Virtuals() : nullptr);
}
} // namespace

void AIBrain::Load(CStream* stream) {
    ReleaseAllModes(modes);
    AssignCurrent(current, nullptr);
    nearX = kNearPointCleared;
    decay = 1.0f;
    nearY = kNearPointCleared;
    int value;
    stream->Read(&value, 4);
    action = value;
    stream->Read(&actionPoint, 12);
    LoadLooks(&looks, stream);
    stream->Read(&actionTime, 4);
    target.Load(stream, TheServerWorld());
    seen.Load(stream, TheServerWorld());
    stream->Read(&callRadius, 4);
    stream->Read(&nearX, 4);
    stream->Read(&nearY, 4);
    stream->Read(&decay, 4);
    stream->Read(&previousPosition, 12);
    stream->Read(&position, 12);
    stream->Read(&alertMode, 4);
    int count;
    int currentIndex;
    stream->Read(&count, 4);
    stream->Read(&currentIndex, 4);
    AssignCurrent(current, nullptr);
    for (int i = 0; i < count; ++i) {
        int type;
        stream->Read(&type, 4);
        AIMode* mode = NewModeOfType(type);
        if (!mode) return;      // an unknown type leaves Load at once (the switch's default)
        mode->Virtuals()->Load(stream);
        // The inlined AddTail: a node holding the mode (RefPtr::ShareAssign).
        auto* node = modes.NewNode(modes.tail, nullptr);
        AsRefPtr(&node->data)->ShareAssign(mode->Virtuals());
        if (modes.tail) modes.tail->next = node;
        else modes.head = node;
        modes.tail = node;
        if (i == currentIndex) AssignCurrent(current, modes.tail->data);
    }
    memory.Load(stream);
    stream->Read(&alerted, 1);
    stream->Read(&alarm, 1);
    stream->Read(&count, 4);
    alarmRecords.SetSize(count, -1);
    for (int i = 0; i < alarmRecords.size; ++i) alarmRecords.data[i].Read(stream);
    stream->Read(&alarmCountdown, 4);
    // A radius saved as the default one becomes the configured one.
    LogicSettings& settings = GameLogicSettings();
    EI_NAN_AUDIT(callRadius, settings.callForHelpRadiusDefault);
    if (callRadius == settings.callForHelpRadiusDefault)
        std::memcpy(&callRadius, &settings.callForHelpRadius, 4);
    stream->Read(&alwaysUpdated, 1);
    if (stream->version_ < 0x4B) return;
    attention.healWeight = 0.0f;
    LoadAttackOptions(&attention.weapons, stream);
    if (stream->version_ >= 0x5A) {
        LoadAttackOptions(&attention.spells, stream);
        stream->Read(&attention.healWeight, 4);
    }
    if (stream->version_ < 0x71) return;
    stream->Read(&alarmSlot, 1);
}
EI_REWRITTEN(0x005D8310, AIBrain::Load);

void AIBrain::Save(CStream* stream) {
    WriteCopy(stream, action);
    stream->Write(&actionPoint, 12);
    SaveLooks(&looks, stream);
    WriteCopy(stream, actionTime);
    target.Save(stream);
    seen.Save(stream);
    WriteCopy(stream, callRadius);
    WriteCopy(stream, nearX);
    WriteCopy(stream, nearY);
    WriteCopy(stream, decay);
    stream->Write(&previousPosition, 12);
    stream->Write(&position, 12);
    stream->Write(&alertMode, 4);
    WriteCopy(stream, modes.count);
    int currentIndex = -1;
    int index = 0;
    for (auto* node = modes.head; node; node = node->next, ++index)
        if (node->data == current) currentIndex = index;
    WriteCopy(stream, currentIndex);
    for (auto* node = modes.head; node;) {
        auto* next = node->next;
        AIModeVirtuals* mode = node->data->Virtuals();
        WriteCopy(stream, mode->GetType());
        mode->Save(stream);
        node = next;
    }
    memory.Save(stream);
    WriteCopy(stream, alerted);
    WriteCopy(stream, alarm);
    WriteCopy(stream, alarmRecords.size);
    for (int i = 0; i < alarmRecords.size; ++i) alarmRecords.data[i].Write(stream);
    WriteCopy(stream, alarmCountdown);
    WriteCopy(stream, alwaysUpdated);
    WriteCopy(stream, attention.weapons.size);
    for (int i = 0; i < attention.weapons.size; ++i) attention.weapons.data[i].Save(stream);
    WriteCopy(stream, attention.spells.size);
    for (int i = 0; i < attention.spells.size; ++i) attention.spells.data[i].Save(stream);
    WriteCopy(stream, attention.healWeight);
    WriteCopy(stream, alarmSlot);
}
EI_REWRITTEN(0x005D8860, AIBrain::Save);
