// The unit AI brain's mode list and state resets, and its save-game
// Load / Save.
#include "map_spec.h"
#include "unit_ai.h"

#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "logic_settings.h"
#include "object_diplomacy.h"
#include "world.h"

#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <new>
#include <string>

namespace {
constexpr float kNearPointCleared = 100000.0f;

float InitialCallRadius() { return 10.0f /*0073FED8*/; }
// A new fear mode's distance (copied as a dword).
std::uint32_t FearDistanceBits() { return 0x41200000u /*0073FF00*/; }

const Vector3* PositionOf(CUnit* unit) { return &unit->localPosition_; }

// The mode list's elements are owning pointers: released with DestroyOwned, added with ShareOwned
// (which may hand out a clone).

// The inlined CList RemoveAll of the mode list, each element released first.
void ReleaseAllModes(std::list<AIMode*>& modes) {
    for (auto node = modes.begin(); node != modes.end(); ++node) DestroyOwned(*node);
    modes.clear();
}

// The inlined RemoveAt: unlink, release the element, return the node to the free list (the last
// node frees the blocks).
void RemoveModeNode(std::list<AIMode*>& modes, std::list<AIMode*>::iterator  node) {
    DestroyOwned(*node);
    modes.erase(node);
}

// The inlined release of a counted object pointer of the memory lists, then RemoveAll.
void ReleaseAllObjects(std::list<CUnit*>& list) {
    for (auto node = list.begin(); node != list.end(); ++node) {
        if (*node) (*node)->Release();
        *node = nullptr;
    }
    list.clear();
}

// The mode in charge loses its reference (RefPtr::Assign(nullptr) / the inlined release).
void DropCurrent(AIMode*& current) {
    AIMode* was = current;
    current = nullptr;
    if (was) was->Release();
}

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

// The inlined RemoveAt of a memory list: unlink, release the element, free the node.
void RemoveObjectNode(std::list<CUnit*>& list, std::list<CUnit*>::iterator  node) {
    if (*node) (*node)->Release();
    *node = nullptr;
    list.erase(node);
}

// Removes every element holding `object` (each time searching again from the head).
void RemoveAllOf(std::list<CUnit*>& list, CEngineObject* object) {
    for (auto node = std::ranges::find(list, object); node != list.end(); node = std::ranges::find(list, object))
        RemoveObjectNode(list, node);
}

// Each scalar is copied into a stack slot before it is written.
template <typename T>
void WriteCopy(CStream* stream, const T& value) {
    T copy = value;
    stream->Write(&copy, sizeof(T));
}

// A new mode of `type` as Load constructs it before reading its data (the fields Load reads stay
// uninitialised). Null for an unknown type (9, or above 10).
AIMode* NewModeOfType(int type) {
    switch (type) {
    case kModeGuard: return new GuardMode();
    case kModePatrol: return new PatrolMode();
    case kModeSentry: return new SentryMode();
    case kModeSuspection: return new SuspectionMode();
    case kModeAggression: return new AggressionMode();
    case kModeCorpseWatcher: return new CorpseWatcherMode();
    case kModePlayer: return new PlayerMode();
    case kModeFear: return new FearMode(0);
    case kModeTamed: return new TamedMode();
    case kModeFollow: return new FollowMode();
    default: return nullptr;
    }
}

void AssignCurrent(AIMode*& current, AIMode* mode) { AssignCounted(current, mode); }

} // namespace

void AIBrain::AddMode(AIMode* mode) {
    int type = mode->GetType();
    bool exclusive = mode->Exclusive();
    for (auto node = modes.begin(); node != modes.end();) {
        auto next = std::next(node);
        AIMode* old = *node;
        if (old->GetType() == type || (exclusive && old->Exclusive())) {
            if (current == *node) DropCurrent(current);
            RemoveModeNode(modes, node);
            break;
        }
        node = next;
    }
    auto node = modes.emplace(modes.end());
    if (mode != *node) {
        DestroyOwned(*node);
        ShareOwned(*node, mode);
    }
}

void AIBrain::RemoveModesOfType(int type) {
    for (auto node = modes.begin(); node != modes.end();) {
        auto next = std::next(node);
        if ((*node)->GetType() == type) RemoveModeNode(modes, node);
        node = next;
    }
}

void AIBrain::ClearModes() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
}

void AIBrain::Reset() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    memory.Reset();
    move.point = Vector3{0.0f, 0.0f, 0.0f};
    move.looks.clear();
    if (target) {
        target->Release();
        target = nullptr;
    }
    if (seen) {
        seen->Release();
        seen = nullptr;
    }
}

void AIBrain::Init() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    memory.Reset();
    move.point = Vector3{0.0f, 0.0f, 0.0f};
    move.looks.clear();
    if (target) target.Reset();
    if (seen) seen.Reset();
    owner = nullptr;
    alerted = 0;
    alarm = 0;
    alarmCountdown = 1;
    alarmSlot = 0;
    actionTime = kNoTime;
    memory.seesEnemy = 0;
    alarmRecords.clear();
    callRadius = InitialCallRadius();
    position = Vector3{0.0f, 0.0f, 0.0f};
    alertMode = 0;
    alwaysUpdated = 0;
    standCooldown = 0;
    followTimer = 0;
    previousPosition = Vector3{0.0f, 0.0f, 0.0f};
}

void AIMemory::Reset() {
    ReleaseAllObjects(nearby);
    ReleaseAllObjects(visible);
    ReleaseAllObjects(seenCorpses);
    ReleaseAllObjects(corpses);
    noticedX = 0.0f;
    noticedY = 0.0f;
    noticedLevel = 0;
    nearbyCountdown = 0;
    reports.clear();
    calls.clear();
    noticedKind = 0;
}

// --- Standard mode sets ---------------------------------------------------------------------------

void AIBrain::AddGuardMode(const Vector3* point, float radius, float wander) {
    AddMode(new GuardMode(point, radius, wander));
}

void AIBrain::AddSuspectionMode() {
    AddMode(new SuspectionMode());   // point and level zeroed
}

void AIBrain::AddAggressionMode() {
    AddMode(new AggressionMode());
}

void AIBrain::AddCorpseWatcherMode() { AddMode(new CorpseWatcherMode()); }

void FearMode::Init(std::uint8_t newFlag) {
    std::uint32_t bits = FearDistanceBits();
    std::memcpy(&distance, &bits, sizeof(bits));
    spread = 3.0f;
    shelter.x = -1313.0f;    // unset
    fleePoint.z = 0.0f;
    flag = newFlag;
}

FearMode::FearMode(std::uint8_t newFlag) {
    ClearFields();   // zeroed (the original leaves what memory held)
    Init(newFlag);
}

void AIBrain::AddFearMode(std::uint8_t flag) { AddMode(new FearMode(flag)); }

void AIBrain::AddSentryMode(const Vector3* point) {
    AddMode(new SentryMode(point));
}

void AIBrain::StandardGuard(const Vector3* point, float radius) {
    ClearModes();
    AddGuardMode(point, radius, -1.0f);
    AddAlertModes(this);
}

void AIBrain::StandardSentry(const Vector3* point) {
    ClearModes();
    AddSentryMode(point);
    AddAlertModes(this);
}

// --- Calls for help -------------------------------------------------------------------------------

void AIBrain::CallAllies(const Vector3* point) {
    std::list<CUnit*> units{};
    CUnit* self = owner->unit;
    TheServerWorld()->Sectors()->FindUnitsInRadius(PositionOf(self), &units, callRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (other == self || !(GameDiplomacy()->allied_[self->side] & (1u << (other->side & 31)))) continue;
        std::string message;
        AIBrain* ally = other->ServerController()->Brain();
        if (console::g_filterMask & 1) {
            double dz = self->localPosition_.z - other->localPosition_.z;
            double dy = self->localPosition_.y - other->localPosition_.y;
            double dx = self->localPosition_.x - other->localPosition_.x;
            message = str::Format("Unit %i call for help unit %i from %fm distance", self->id_, other->id_,
                           std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        }
        ally->memory.HearCall(point, 0x1A, message.c_str());
    }
    // The list's RemoveAll: no element destruction.
    units.clear();
}

// --- Perception memory ----------------------------------------------------------------------------

float* CopyPoint2(float* out, const float* point) {
    float x = point[0];
    float y = point[1];
    out[0] = x;
    out[1] = y;
    return out;
}

int AIMemory::Notice(const Vector3* point, int level, int kind) {
    if (level <= noticedLevel) return 0;
    int changed = 1;
    if (static_cast<int>(static_cast<unsigned>(level) - static_cast<unsigned>(noticedLevel)) <= 100) {
        double dx = point->x - noticedX;
        double dy = point->y - noticedY;
        double z = point->z;
        // Farther than the limit; NaN is not.
        changed = z * z + dy * dy + dx * dx > 10.0 ? 1 : 0;
    }
    noticedLevel = level;
    noticedX = point->x;
    noticedY = point->y;
    noticedKind = kind;
    return changed;
}

void AIMemory::Forget(CEngineObject* object) {
    AIRef held{nullptr};
    held.Assign(object);
    RemoveAllOf(nearby, held.object);
    RemoveAllOf(visible, held.object);
    RemoveAllOf(seenCorpses, held.object);
    RemoveAllOf(corpses, held.object);
    std::erase_if(reports, [object](const AIReport& entry) { return entry.object == object; });
    held.Reset();
}

void CUnitServer::AddHostileMask(std::uint32_t mask) { unit->AddHostileMask(mask); }

void AIHeardCalls::Update(CUnit* unit, AIMemory* memory) {
    for (auto call = begin(); call != end();) {
        if (--call->kind >= 0) {
            ++call;
            continue;
        }
        if (call->hostileMask != 0) {
            console::EndLine(AiLog() << "Unit " << unit->id_ << " hear delayed sound: " << call->text.c_str());
            unit->ServerController()->AddHostileMask(static_cast<std::uint32_t>(call->hostileMask));
        } else if (memory->noticedLevel < 900) {
            // The inlined Notice of level 900: logged when the level rises by more than 100 or the point is
            // more than sqrt(10) away (NaN is near).
            bool changed = true;
            if (900 - memory->noticedLevel <= 100) {
                float dx = call->point.x - memory->noticedX;
                float dy = call->point.y - memory->noticedY;
                float dz = call->point.z - 0.0f;
                changed = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) > 10.0f;
            }
            float copy[2];
            const float* point = CopyPoint2(copy, &call->point.x);
            memory->noticedX = point[0];
            memory->noticedLevel = 900;
            memory->noticedY = point[1];
            memory->noticedKind = -3;
            if (changed) console::EndLine(AiLog() << "Delayed sound: " << call->text.c_str());
        }
        call = erase(call);
    }
}

void AIMemory::Update(CUnitServer* who) {
    std::erase_if(reports, [](AIReport& entry) { return --entry.what < 0; });
    calls.Update(who->unit, this);
    TheServerWorld()->AIEvents()->Update(who, this);
    noticedLevel += noticedKind;
}

int AIMemory::IsEnemy(CUnit* self, CEngineObject* other) {
    auto* unit = static_cast<CUnit*>(other);
    if (self->side == unit->side || self->player == unit->player) return 1;
    return lib::Contains(visible, other) ? 1 : 0;
}

void AIReports::Report(CMapObject* object, int what) {
    if (!object) return;
    for (AIReport& entry : *this) {
        if (entry.object == object) {
            if (entry.what <= what) entry.what = what;
            return;
        }
    }
    push_back({object, what});
}

bool AIReports::Contains(CEngineObject* object) const {
    return std::ranges::any_of(*this, [object](const AIReport& entry) { return entry.object == object; });
}

// --- Save games -----------------------------------------------------------------------------------

void AIBrain::Load(CStream* stream) {
    ReleaseAllModes(modes);
    AssignCurrent(current, nullptr);
    nearX = kNearPointCleared;
    decay = 1.0f;
    nearY = kNearPointCleared;
    int value;
    stream->Read(&value, 4);
    action = value;
    stream->Read(&move.point, 12);
    LoadLooks(&move.looks, stream);
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
        mode->Load(stream);
        // The inlined AddTail: a node holding the mode (RefPtr::ShareAssign).
        auto node = modes.emplace(modes.end());
        ShareAssignOwned(*node, mode);
        if (i == currentIndex) AssignCurrent(current, modes.back());
    }
    memory.Load(stream);
    stream->Read(&alerted, 1);
    stream->Read(&alarm, 1);
    stream->Read(&count, 4);
    alarmRecords.resize(count);
    for (int i = 0; i < std::ssize(alarmRecords); ++i) alarmRecords[i].Read(stream);
    stream->Read(&alarmCountdown, 4);
    // A radius saved as the default one becomes the configured one.
    LogicSettings& settings = g_logicSettings;
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

void AIBrain::Save(CStream* stream) {
    WriteCopy(stream, action);
    stream->Write(&move.point, 12);
    SaveLooks(&move.looks, stream);
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
    WriteCopy(stream, std::ssize(modes));
    int currentIndex = -1;
    int index = 0;
    for (auto node = modes.begin(); node != modes.end(); ++node, ++index)
        if (*node == current) currentIndex = index;
    WriteCopy(stream, currentIndex);
    for (auto node = modes.begin(); node != modes.end();) {
        auto next = std::next(node);
        AIMode* mode = *node;
        WriteCopy(stream, mode->GetType());
        mode->Save(stream);
        node = next;
    }
    memory.Save(stream);
    WriteCopy(stream, alerted);
    WriteCopy(stream, alarm);
    WriteCopy(stream, std::ssize(alarmRecords));
    for (int i = 0; i < std::ssize(alarmRecords); ++i) alarmRecords[i].Write(stream);
    WriteCopy(stream, alarmCountdown);
    WriteCopy(stream, alwaysUpdated);
    WriteCopy(stream, std::ssize(attention.weapons));
    for (int i = 0; i < std::ssize(attention.weapons); ++i) attention.weapons[i].Save(stream);
    WriteCopy(stream, std::ssize(attention.spells));
    for (int i = 0; i < std::ssize(attention.spells); ++i) attention.spells[i].Save(stream);
    WriteCopy(stream, attention.healWeight);
    WriteCopy(stream, alarmSlot);
}
