// The network state writers of map objects and units: the virtual that writes an object's dirty fields
// into its DeltaState for the clients. See docs/notes/units.md ("Network state").
#include "map_object_figures.h"
#include "net_delta.h"

#include "ai_map.h"
#include "ai_motion.h"
#include "map_object.h"
#include "player_client.h"
#include "player_messages.h"
#include "unit.h"
#include "world.h"


#include <cstdint>
#include <cstring>
#include <string>

namespace {

void PutByte(DeltaField* field, std::uint8_t value) { field->WriteDelta(&value, 1); }
void PutWord(DeltaField* field, std::uint32_t value) { field->WriteDelta(&value, 4); }

// Whether the stream read against (delta::g_readSnapshot) is consumed: Position() == Size().
std::uint32_t SnapshotConsumed() {
    MemoryStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    std::uint32_t size = delta::g_readSnapshot->Size();
    return position == size ? 1u : 0u;
}

constexpr float kStatEpsilon = 1e-4f;

// A body part's condition as current / maximum (signed 16-bit over unsigned 16-bit), as a float.
float StatFraction(Unit* unit, int k) {
    const UnitParamsSlot& part = unit->params.slots[k];   // + k * 0x1C: current, maximum
    return static_cast<float>(part.current) / static_cast<float>(part.maximum);
}

// fabs(v) > epsilon (false for a NaN).
bool Exceeds(float v) {
    float a = v < 0 ? -v : v;
    return a > kStatEpsilon;
}

} // namespace

void MapObject::WriteNetState() {
    DeltaState* state = &netState;
    if (DeltaField* field = state->BeginField(0)) {
        field->WriteDelta(&packedVitals, 0x18);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(1)) {
        field->WriteDelta(&scale, 0xC);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        PutByte(field, static_cast<std::uint8_t>((flags >> 3) & 1));
        field->WriteDelta(&localPosition.x, 0xC);
        field->WriteDelta(&localRotation, 0x10);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        PutByte(field, static_cast<std::uint8_t>(std::ssize(effects)));
        for (int i = 0; i < std::ssize(effects); ++i) {
            MapEffect* effect = &effects[i];
            field->WriteBoth(&effect->type, 1);
            field->WriteBoth(&effect->power, 1);
            field->WriteBoth(&effect->ticksLeft, 2);
            field->WriteBoth(&effect->ending, 1);
        }
        field->EndField();
    }
}

void Unit::WriteNetState() {
    MapObject::WriteNetState();
    Unit* unit = this;
    DeltaState* state = &netState;
    if (DeltaField* field = state->BeginField(4)) {
        field->WriteDelta(&unit->params, 0x108);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        auto ids = unit->GetEquipmentIds();
        for (int i = 0; i < 4; ++i) PutWord(field, ids[i]);
        PutWord(field, (unit->flags & MapObjectFlag::HelmetHidden) ? 0xFFFFFFFFu : ids[4]);
        for (int i = 5; i < 11; ++i) PutWord(field, ids[i]);
        field->EndField();
        if (unit->controller->GetClassId() == ClassId::UnitServer)
            unit->Controller()->RefreshEquipment();
    }
    if (DeltaField* field = state->BeginField(6)) {
        field->WriteDelta(&unit->moveMode, 4);
        field->WriteDelta(&unit->moveMode2, 4);
        PutWord(field, unit->activeWeapon);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(7)) {
        // +0x278 (1 bit), +0x274 dead (1 bit), +0x260 (3 bits), +0x25C (3 bits): their low bytes.
        std::uint8_t bits = static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>((static_cast<std::uint8_t>(static_cast<std::uint8_t>(unit->obstacle) << 1) |
                                        (static_cast<std::uint8_t>(unit->dead) & 1))
                                       << 3) |
             (static_cast<std::uint8_t>(unit->pathTimeShift) & 7))
            << 3);
        bits = static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(unit->motionMode) & 7));
        PutByte(field, bits);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(8)) {
        PutByte(field, static_cast<std::uint8_t>(unit->animationEntry));
        PutByte(field, static_cast<std::uint8_t>(unit->secondEntry));
        PutWord(field, static_cast<std::uint32_t>(unit->animationStart));
        PutByte(field, static_cast<std::uint8_t>(unit->actionSound));
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        field->WriteStringBoth(unit->scriptName);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        PathOrder* path = unit->Controller()->Path();
        path->WriteState(field);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(11)) {
        Player* target = unit->player;
        std::uint32_t objectId = target ? static_cast<std::uint32_t>(target->id) : 0xFFFFFFFFu;
        PutWord(field, unit->relationsB & unit->relationsA);
        PutByte(field, static_cast<std::uint8_t>(unit->side));
        PutWord(field, objectId);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(12)) {
        field->WriteDelta(unit->exposure.data(), 0x14);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(13)) {
        field->WriteDelta(&unit->fightAtWill, 4);
        field->EndField();
    }
}


std::uint32_t MapObject::ReadNetState(int first) {
    DeltaState* state = &netState;
    std::uint32_t complete = 1;
    if (DeltaField* field = state->BeginField(0)) {
        short before = packedVitals.hitPoints;
        field->ReadDelta(&packedVitals, 0x18);
        complete = SnapshotConsumed();
        field->EndField();
        if (before != packedVitals.hitPoints || first == 1) RefreshWounds();
    }
    if (DeltaField* field = state->BeginField(1)) {
        Vector3 figureScale;
        field->ReadDelta(&figureScale, 0xC);
        SetFigureScale(&figureScale.x);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        char hidden;
        field->ReadDelta(&hidden, 1);
        if (hidden)
            flags |= 8;
        else
            flags &= ~8u;
        Vector3 position;
        field->ReadDelta(&position, 0xC);
        field->ReadDelta(&localRotation, 0x10);
        World* world = OwningWorld();
        world->MoveObject(this, &position.x);
        PlaceOnGround(localPosition.z);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    DeltaField* field = state->BeginField(3);
    if (!field) return complete;
    std::uint32_t countWord = 0;   // (one byte read into a word whose upper bytes the original leaves)
    field->ReadDelta(&countWord, 1);
    int count = static_cast<int>(countWord & 0xFF);
    SetEffectCount(count);
    for (int i = 0; i < std::ssize(effects); ++i) {
        MapEffect* effect = &effects[i];
        effect->ticksLeft = 0;
        field->ReadRaw(&effect->type, 1);
        field->ReadRaw(&effect->power, 1);
        field->ReadRaw(&effect->ticksLeft, 2);
        field->ReadRaw(&effect->ending, 1);
    }
    if (GetClassId() != ClassId::Unit)
        companion->EffectsChanged(first == 0);
    complete &= SnapshotConsumed();
    field->EndField();
    return complete;
}

std::uint32_t Unit::ReadNetState(int first) {
    Unit* unit = this;
    float hpBefore = static_cast<float>(unit->packedVitals.hitPoints) / static_cast<float>(unit->packedVitals.maxHitPoints);
    std::uint32_t complete = MapObject::ReadNetState(first);
    DeltaState* state = &netState;
    bool gotItems = false;
    bool changed = false;
    std::uint32_t ids4[4];
    std::uint32_t ids7[7];
    if (DeltaField* field = state->BeginField(4)) {
        float before[6];
        for (int k = 0; k < 6; ++k) before[k] = StatFraction(unit, k);
        field->ReadDelta(&unit->params, 0x108);
        // IsDead: a dead unit skips the stat reactions.
        if (!unit->IsDead() && first == 1) {
            bool any = false;
            for (int k = 0; k < 6; ++k) {
                float now = StatFraction(unit, k);
                if (Exceeds(now - before[k])) any = true;
                float change = before[k] - now;
                if (Exceeds(change)) {
                    unit->Controller()->StatChanged(k, change, unit->params.flags);
                }
            }
            if (any) unit->Controller()->RefreshWounds();
        } else if (first == 0) {
            unit->Controller()->RefreshWounds();
        }
        float maxHp = static_cast<float>(unit->packedVitals.maxHitPoints);
        float hpNow = static_cast<float>(unit->packedVitals.hitPoints) / maxHp;
        // A drop (before > now, ordered), or UnitStatsFlag::Struck otherwise.
        bool dropped = hpBefore > hpNow;
        if ((dropped || (unit->params.flags & UnitStatsFlag::Struck)) && first != 0) {
            constexpr UnitStatsFlag kHitKinds =
                UnitStatsFlag::Backstab | UnitStatsFlag::Head | UnitStatsFlag::Electrical;
            std::uint32_t kind = unit->params.flags & kHitKinds;
            float lost = hpBefore - hpNow;
            int damage = static_cast<int>(static_cast<std::int64_t>(lost * maxHp));   // _ftol
            unit->companion->Hit(damage, static_cast<int>(kind));
            if (kind & UnitStatsFlag::Electrical) {
                unit->Controller()->Wound();
            }
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        unit->ReleaseAllItems();
        for (auto& fieldId : ids4) field->ReadDelta(&fieldId, 4);
        for (auto& fieldId : ids7) field->ReadDelta(&fieldId, 4);
        gotItems = true;
        changed = true;
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(6)) {
        field->ReadDelta(&unit->moveMode, 4);
        field->ReadDelta(&unit->moveMode2, 4);
        std::uint32_t value;
        field->ReadDelta(&value, 4);
        if (unit->activeWeapon != value) {
            changed = true;
            unit->Controller()->ShowWeapon(value);
        }
        unit->pendingWeapon = static_cast<int>(unit->activeWeapon);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(7)) {
        std::uint8_t bits;
        field->ReadDelta(&bits, 1);
        unit->motionMode = static_cast<signed char>(bits << 5) >> 5;
        unit->pathTimeShift = static_cast<signed char>(bits << 2) >> 5;
        unit->dead = static_cast<signed char>(bits << 1) >> 7;
        unit->obstacle = static_cast<std::uint32_t>(static_cast<signed char>(bits) >> 7);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(8)) {
        field->ReadDelta(&unit->animationEntry, 1);
        field->ReadDelta(&unit->secondEntry, 1);
        field->ReadDelta(&unit->animationStart, 4);
        field->ReadDelta(&unit->actionSound, 1);
        if (unit->motionMode == 1) --unit->animationStart;
        float time = static_cast<float>(unit->OwningWorld()->tick);
        unit->Controller()->RestartAnimation(time);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        unit->scriptName = field->ReadString();
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        PathOrder* path = unit->Controller()->Path();
        path->ReadState(field);
        if (unit->pathTimeShift == 1) path->ShiftTime(-1);
        PathOrder* path2 = unit->Controller()->Path();
        path2->ForgetObject(unit->id);
        const Unit::MoveSpeeds speeds = unit->GetMoveSpeeds();
        const float walk = speeds.forward, run = speeds.turn;
        UnitMotion* walker = &unit->motion;
        walker->segment = -1;
        walker->speedScale = walk;
        walker->baseTurnRate = run;
        walker->Build(path2);
        unit->MarkChanged(10);
        unit->Controller()->MotionChanged();
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(11)) {
        field->ReadDelta(&unit->relationsA, 4);
        unit->side = 0;
        field->ReadDelta(&unit->side, 1);
        int objectId;
        field->ReadDelta(&objectId, 4);
        Player* current = unit->player;
        if (objectId == -1) {
            if (current) {
                unit->player = nullptr;
                current->RemoveUnit(unit);
                if (Player* now = unit->player) now->AddUnit(unit);
                unit->MarkChanged(11);
                if (unit->controller) unit->Controller()->PlayerChanged(nullptr);
            }
        } else if (!current || current->id != objectId) {
            PlayerClient* view = unit->ClientWorld()->LocalView();
            Player* local = view->player;
            if ((local->id == objectId || view->localSession) && unit->player != local) {
                Player* old = unit->player;
                unit->player = local;
                if (old) old->RemoveUnit(unit);
                if (Player* now = unit->player) now->AddUnit(unit);
                unit->MarkChanged(11);
                if (unit->controller) unit->Controller()->PlayerChanged(local);
            }
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(12)) {
        field->ReadDelta(unit->exposure.data(), 0x14);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(13)) {
        field->ReadDelta(&unit->fightAtWill, 4);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        unit->companion->EffectsChanged(first == 0);
        field->EndField();
    }
    if (changed) {
        if (gotItems) {
            unit->Controller()->SetArmourModels(ids7);
            unit->Controller()->SetWeaponModels(ids4);
        }
        unit->Controller()->ShowWeapon(unit->activeWeapon);
        unit->Controller()->RefreshEquipment();
    }
    return complete;
}
