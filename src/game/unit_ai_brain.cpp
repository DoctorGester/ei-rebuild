// The unit AI brain's mode list and state resets, and its save-game
// Load / Save.
#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "game_singletons.h"
#include "logic_settings.h"
#include "map_spec.h"
#include "object_diplomacy.h"
#include "save_versions.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <new>
#include <string>

namespace {
// A new fear mode's distance (copied bit for bit).
std::uint32_t FearDistanceBits() { return 0x41200000u; }


// The mode list's elements are owning pointers: released with DestroyOwned, added with ShareOwned
// (which may hand out a clone).

// Empties the mode list, each element released first.
void ReleaseAllModes(std::list<AIMode*>& modes) {
    for (auto& node : modes) DestroyOwned(node);
    modes.clear();
}

// Removes one node: unlink, release the element, return the node to the free list (the last
// node frees the blocks).
void RemoveModeNode(std::list<AIMode*>& modes, std::list<AIMode*>::iterator  node) {
    DestroyOwned(*node);
    modes.erase(node);
}

// The tail both standard sets share.
void AddAlertModes(AIBrain* brain) {
    brain->AddSuspectionMode();
    brain->AddAggressionMode();
    brain->AddCorpseWatcherMode();
    brain->AddFearMode(0);
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
}

// Removes every element holding `object`, head first.
void RemoveAllOf(HeldUnits& list, CEngineObject* object) {
    for (auto node = list.begin(); node != list.end();) node = node->Get() == object ? list.erase(node) : std::next(node);
}

// A new mode of `type` as Load constructs it before reading its data (the fields Load reads stay
// uninitialised). Null for an unknown type (9, or above 10).
AIMode* NewModeOfType(int type) {
    switch (static_cast<AIModeType>(type)) {
    case AIModeType::Guard: return new GuardMode();
    case AIModeType::Patrol: return new PatrolMode();
    case AIModeType::Sentry: return new SentryMode();
    case AIModeType::Suspection: return new SuspectionMode();
    case AIModeType::Aggression: return new AggressionMode();
    case AIModeType::CorpseWatcher: return new CorpseWatcherMode();
    case AIModeType::Player: return new PlayerMode();
    case AIModeType::Fear: return new FearMode(0);
    case AIModeType::Tamed: return new TamedMode();
    case AIModeType::Follow: return new FollowMode();
    default: return nullptr;
    }
}

} // namespace

void AIBrain::AddMode(AIMode* mode) {
    AIModeType type = mode->GetType();
    bool exclusive = mode->Exclusive();
    for (auto node = modes.begin(); node != modes.end();) {
        auto next = std::next(node);
        AIMode* old = *node;
        if (old->GetType() == type || (exclusive && old->Exclusive())) {
            if (current == *node) current.Reset();
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

void AIBrain::RemoveModesOfType(AIModeType type) {
    for (auto node = modes.begin(); node != modes.end();) {
        auto next = std::next(node);
        if ((*node)->GetType() == type) RemoveModeNode(modes, node);
        node = next;
    }
}

void AIBrain::ClearModes() {
    ReleaseAllModes(modes);
    current.Reset();
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
}

void AIBrain::Reset() {
    ReleaseAllModes(modes);
    current.Reset();
    nearX = kNearPointCleared;
    nearY = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    memory.Reset();
    move.point = Vector3{0.0f, 0.0f, 0.0f};
    move.looks.clear();
    target.Reset();
    seen.Reset();
}

void AIBrain::Init() {
    ReleaseAllModes(modes);
    current.Reset();
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
    callRadius = kInitialCallRadius;
    position = Vector3{0.0f, 0.0f, 0.0f};
    alertMode = 0;
    alwaysUpdated = 0;
    standCooldown = 0;
    followTimer = 0;
    previousPosition = Vector3{0.0f, 0.0f, 0.0f};
}

void AIMemory::Reset() {
    nearby.clear();
    visible.clear();
    seenCorpses.clear();
    corpses.clear();
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
    distance = std::bit_cast<float>(bits);
    spread = 3.0f;
    shelter.x = -1313.0f;    // unset
    fleePoint.z = 0.0f;
    flag = newFlag;
}

FearMode::FearMode(std::uint8_t newFlag) { Init(newFlag); }

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
    g_serverWorld->aiMap_.sectorMap.FindUnitsInRadius(&self->localPosition_, &units, callRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (other == self || !g_serverWorld->diplomacy_.IsAllied(self->side, other->side)) continue;
        std::string message;
        AIBrain* ally = &other->ServerController()->brain;
        if (console::g_filterMask & 1) {
            double dz = self->localPosition_.z - other->localPosition_.z;
            double dy = self->localPosition_.y - other->localPosition_.y;
            double dx = self->localPosition_.x - other->localPosition_.x;
            message = str::Format("Unit %i call for help unit %i from %fm distance", self->id_, other->id_,
                           std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        }
        ally->memory.HearCall(point, 0x1A, message);
    }
    // The list emptied, no element destruction.
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
    HeldRef<CEngineObject> held(object);   // kept while its entries are removed
    RemoveAllOf(nearby, object);
    RemoveAllOf(visible, object);
    RemoveAllOf(seenCorpses, object);
    RemoveAllOf(corpses, object);
    std::erase_if(reports, [object](const AIReport& entry) { return entry.object == object; });
}

void CUnitServer::AddHostileMask(std::uint32_t mask) { unit->AddHostileMask(mask); }

void AIHeardCalls::Update(CUnit* unit, AIMemory* memory) {
    for (auto call = begin(); call != end();) {
        if (--call->kind >= 0) {
            ++call;
            continue;
        }
        if (call->hostileMask != 0) {
            console::EndLine(kAiLog << "Unit " << unit->id_ << " hear delayed sound: " << call->text);
            unit->ServerController()->AddHostileMask(static_cast<std::uint32_t>(call->hostileMask));
        } else if (memory->noticedLevel < 900) {
            // Notice of level 900: logged when the level rises by more than 100 or the point is
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
            if (changed) console::EndLine(kAiLog << "Delayed sound: " << call->text);
        }
        call = erase(call);
    }
}

void AIMemory::Update(CUnitServer* who) {
    std::erase_if(reports, [](AIReport& entry) { return --entry.what < 0; });
    calls.Update(who->unit, this);
    g_serverWorld->aiMap_.events.Update(who, this);
    noticedLevel += noticedKind;
}

bool AIMemory::IsEnemy(CUnit* self, CEngineObject* other) {
    auto* unit = static_cast<CUnit*>(other);
    if (self->side == unit->side || self->player == unit->player) return true;
    return lib::Contains(visible, other);
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
    current.Reset();
    nearX = kNearPointCleared;
    decay = 1.0f;
    nearY = kNearPointCleared;
    int value;
    stream->ReadValue(value);
    action = value;
    stream->ReadValue(move.point);
    LoadLooks(&move.looks, stream);
    stream->ReadValue(actionTime);
    LoadRef(target, stream, &g_serverWorld.Get());
    LoadRef(seen, stream, &g_serverWorld.Get());
    stream->ReadValue(callRadius);
    stream->ReadValue(nearX);
    stream->ReadValue(nearY);
    stream->ReadValue(decay);
    stream->ReadValue(previousPosition);
    stream->ReadValue(position);
    stream->ReadValue(alertMode);
    int count;
    int currentIndex;
    stream->ReadValue(count);
    stream->ReadValue(currentIndex);
    current.Reset();
    for (int i = 0; i < count; ++i) {
        int type;
        stream->ReadValue(type);
        AIMode* mode = NewModeOfType(type);
        if (!mode) return;      // an unknown type leaves Load at once (the switch's default)
        mode->Load(stream);
        // Appends a node holding the mode (RefPtr::ShareAssign).
        auto node = modes.emplace(modes.end());
        ShareAssignOwned(*node, mode);
        if (i == currentIndex) current.Assign(modes.back());
    }
    memory.Load(stream);
    stream->ReadValue(alerted);
    stream->ReadValue(alarm);
    stream->ReadValue(count);
    alarmRecords.resize(count);
    for (int i = 0; i < std::ssize(alarmRecords); ++i) alarmRecords[i].Read(stream);
    stream->ReadValue(alarmCountdown);
    // A radius saved as the default one becomes the configured one.
    LogicSettings& settings = g_logicSettings;
    EI_NAN_AUDIT(callRadius, settings.callForHelpRadiusDefault);
    if (callRadius == settings.callForHelpRadiusDefault)
        callRadius = settings.callForHelpRadius;
    stream->ReadValue(alwaysUpdated);
    if (stream->version_ < kVersionAttackOptions) return;
    attention.healWeight = 0.0f;
    LoadAttackOptions(&attention.weapons, stream);
    if (stream->version_ >= kVersionSpellOptions) {
        LoadAttackOptions(&attention.spells, stream);
        stream->ReadValue(attention.healWeight);
    }
    if (stream->version_ < kVersionAlarmSlot) return;
    stream->ReadValue(alarmSlot);
}

void AIBrain::Save(CStream* stream) {
    stream->WriteValue(action);
    stream->WriteValue(move.point);
    SaveLooks(&move.looks, stream);
    stream->WriteValue(actionTime);
    SaveRef(target, stream);
    SaveRef(seen, stream);
    stream->WriteValue(callRadius);
    stream->WriteValue(nearX);
    stream->WriteValue(nearY);
    stream->WriteValue(decay);
    stream->WriteValue(previousPosition);
    stream->WriteValue(position);
    stream->WriteValue(alertMode);
    stream->WriteValue(std::ssize(modes));
    int currentIndex = -1;
    int index = 0;
    for (auto node = modes.begin(); node != modes.end(); ++node, ++index)
        if (*node == current) currentIndex = index;
    stream->WriteValue(currentIndex);
    for (auto node = modes.begin(); node != modes.end();) {
        auto next = std::next(node);
        AIMode* mode = *node;
        stream->WriteValue(mode->GetType());
        mode->Save(stream);
        node = next;
    }
    memory.Save(stream);
    stream->WriteValue(alerted);
    stream->WriteValue(alarm);
    stream->WriteValue(std::ssize(alarmRecords));
    for (int i = 0; i < std::ssize(alarmRecords); ++i) alarmRecords[i].Write(stream);
    stream->WriteValue(alarmCountdown);
    stream->WriteValue(alwaysUpdated);
    stream->WriteValue(std::ssize(attention.weapons));
    for (int i = 0; i < std::ssize(attention.weapons); ++i) attention.weapons[i].Save(stream);
    stream->WriteValue(std::ssize(attention.spells));
    for (int i = 0; i < std::ssize(attention.spells); ++i) attention.spells[i].Save(stream);
    stream->WriteValue(attention.healWeight);
    stream->WriteValue(alarmSlot);
}
