// The network state writers of map objects and units: the virtual that writes an object's dirty fields
// into its DeltaState for the clients. See docs/notes/units.md ("Network state").
#include "game/objects/object3d.h"
#include "game/net/net_delta.h"

#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"
#include "game/objects/map_object.h"
#include "game/players/player_client.h"
#include "game/players/player_messages.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include <array>
#include <cstdint>

namespace {

// One value as its own bytes; the wire holds the value's size.
template <typename T>
void Put(DeltaWriter& field, T value) {
    field.WriteDelta(&value, sizeof value);
}

constexpr float kStatEpsilon = 1e-4f;

// A body part's condition as current / maximum (signed 16-bit over unsigned 16-bit), as a float.
float StatFraction(const Unit* unit, int k) {
    const UnitParamsSlot& part = unit->params.slots[k];
    return static_cast<float>(part.current) / static_cast<float>(part.maximum);
}

// fabs(v) > epsilon (false for a NaN).
bool Exceeds(float v) {
    float a = v < 0 ? -v : v;
    return a > kStatEpsilon;
}

static_assert(sizeof(Vector3) == 0xC && sizeof(Quaternion) == 0x10 && sizeof(UnitParams) == 0x108);

} // namespace

void MapObject::WriteNetState() {
    if (auto field = netState.WriteField(NetField::Vitals)) {
        field->WriteDelta(&packedVitals, sizeof packedVitals);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Scale)) {
        field->WriteDelta(&scale, sizeof scale);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Placement)) {
        Put<std::uint8_t>(*field, lib::Has(flags, MapObjectFlag::Hidden));
        field->WriteDelta(&localPosition, sizeof localPosition);
        field->WriteDelta(&localRotation, sizeof localRotation);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Effects)) {
        Put<std::uint8_t>(*field, static_cast<std::uint8_t>(std::ssize(effects)));
        for (MapEffect& effect : effects) {
            field->WriteBoth(&effect.type, 1);
            field->WriteBoth(&effect.power, 1);
            field->WriteBoth(&effect.ticksLeft, 2);   // the low 16 bits
            field->WriteBoth(&effect.ending, 1);
        }
        field->End();
    }
}

void Unit::WriteNetState() {
    MapObject::WriteNetState();
    if (auto field = netState.WriteField(NetField::Stats)) {
        field->WriteDelta(&params, sizeof params);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Equipment)) {
        // The eleven slots as they are, except slot 4 (the helmet), which is none while it is hidden.
        std::array<std::uint32_t, 11> ids = GetEquipmentIds();
        if (lib::Has(flags, MapObjectFlag::HelmetHidden)) ids[4] = 0xFFFFFFFFu;
        for (std::uint32_t slotId : ids) Put(*field, slotId);
        field->End();
        if (controller->GetClassId() == ClassId::UnitServer) controller->RefreshEquipment();
    }
    if (auto field = netState.WriteField(NetField::Modes)) {
        field->WriteDelta(&moveMode, sizeof moveMode);
        field->WriteDelta(&moveMode2, sizeof moveMode2);
        Put(*field, activeWeapon);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Motion)) {
        // One byte, from the top: obstacle (1 bit), dead (1), pathTimeShift (3), motionMode (3).
        Put<std::uint8_t>(*field, static_cast<std::uint8_t>(((obstacle & 1) << 7) | ((dead & 1) << 6) |
                                                           ((pathTimeShift & 7) << 3) | (motionMode & 7)));
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Animation)) {
        Put(*field, animationEntry);
        Put(*field, secondEntry);
        Put(*field, animationStart);
        Put(*field, actionSound);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::ScriptName)) {
        field->WriteStringBoth(scriptName);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Path)) {
        controller->Path()->WriteState(*field);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Relations)) {
        Put(*field, relationsB & relationsA);
        Put(*field, static_cast<std::uint8_t>(side));
        Put(*field, player ? static_cast<std::uint32_t>(player->id) : 0xFFFFFFFFu);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::Exposure)) {
        field->WriteDelta(exposure.data(), sizeof exposure);
        field->End();
    }
    if (auto field = netState.WriteField(NetField::FightAtWill)) {
        field->WriteDelta(&fightAtWill, sizeof fightAtWill);
        field->End();
    }
}

bool MapObject::ReadNetState(bool update) {
    bool complete = true;
    if (auto field = netState.ReadField(NetField::Vitals)) {
        short before = packedVitals.hitPoints;
        field->ReadDelta(&packedVitals, sizeof packedVitals);
        complete = field->SnapshotConsumed();
        field->End();
        if (before != packedVitals.hitPoints || update) RefreshWounds();
    }
    if (auto field = netState.ReadField(NetField::Scale)) {
        Vector3 figureScale;
        field->ReadDelta(&figureScale, sizeof figureScale);
        SetFigureScale(figureScale);
        complete &= field->SnapshotConsumed();
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Placement)) {
        std::uint8_t hidden;
        field->ReadDelta(&hidden, 1);
        if (hidden) flags |= MapObjectFlag::Hidden;
        else flags &= ~MapObjectFlag::Hidden;
        Vector3 position;
        field->ReadDelta(&position, sizeof position);
        field->ReadDelta(&localRotation, sizeof localRotation);
        OwningWorld()->MoveObject(this, &position.x);
        PlaceOnGround(localPosition.z);
        complete &= field->SnapshotConsumed();
        field->End();
    }
    std::optional<DeltaReader> field = netState.ReadField(NetField::Effects);
    if (!field) return complete;
    std::uint8_t count = 0;
    field->ReadDelta(&count, 1);
    SetEffectCount(count);
    for (MapEffect& effect : effects) {
        effect.ticksLeft = 0;
        field->ReadRaw(&effect.type, 1);
        field->ReadRaw(&effect.power, 1);
        field->ReadRaw(&effect.ticksLeft, 2);
        field->ReadRaw(&effect.ending, 1);
    }
    if (GetClassId() != ClassId::Unit) companion->EffectsChanged(!update);
    complete &= field->SnapshotConsumed();
    field->End();
    return complete;
}

bool Unit::ReadNetState(bool update) {
    float hpBefore = static_cast<float>(packedVitals.hitPoints) / static_cast<float>(packedVitals.maxHitPoints);
    bool complete = MapObject::ReadNetState(update);
    bool gotItems = false;
    bool changed = false;
    std::uint32_t weaponIds[4];
    std::uint32_t armourIds[7];
    if (auto field = netState.ReadField(NetField::Stats)) {
        ReadNetStats(*field, update, hpBefore);
        complete &= field->SnapshotConsumed();
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Equipment)) {
        ReleaseAllItems();
        for (std::uint32_t& slotId : weaponIds) field->ReadDelta(&slotId, 4);
        for (std::uint32_t& slotId : armourIds) field->ReadDelta(&slotId, 4);
        gotItems = true;
        changed = true;
        complete &= field->SnapshotConsumed();
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Modes)) {
        field->ReadDelta(&moveMode, sizeof moveMode);
        field->ReadDelta(&moveMode2, sizeof moveMode2);
        std::uint32_t weapon;
        field->ReadDelta(&weapon, 4);
        if (activeWeapon != weapon) {
            changed = true;
            controller->ShowWeapon(weapon);
        }
        pendingWeapon = static_cast<int>(activeWeapon);
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Motion)) {
        // The bits WriteNetState packed, each sign-extended from its width.
        std::uint8_t bits;
        field->ReadDelta(&bits, 1);
        motionMode = static_cast<signed char>(bits << 5) >> 5;
        pathTimeShift = static_cast<signed char>(bits << 2) >> 5;
        dead = static_cast<signed char>(bits << 1) >> 7;
        obstacle = static_cast<std::uint32_t>(static_cast<signed char>(bits) >> 7);
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Animation)) {
        field->ReadDelta(&animationEntry, 1);
        field->ReadDelta(&secondEntry, 1);
        field->ReadDelta(&animationStart, 4);
        field->ReadDelta(&actionSound, 1);
        if (motionMode == 1) --animationStart;
        controller->RestartAnimation(static_cast<float>(OwningWorld()->tick));
        field->End();
    }
    if (auto field = netState.ReadField(NetField::ScriptName)) {
        scriptName = field->ReadString();
        complete &= field->SnapshotConsumed();
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Path)) {
        ReadNetPath(*field);
        complete &= field->SnapshotConsumed();
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Relations)) {
        ReadNetRelations(*field);
        complete &= field->SnapshotConsumed();
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Exposure)) {
        field->ReadDelta(exposure.data(), sizeof exposure);
        field->End();
    }
    if (auto field = netState.ReadField(NetField::FightAtWill)) {
        field->ReadDelta(&fightAtWill, sizeof fightAtWill);
        field->End();
    }
    if (auto field = netState.ReadField(NetField::Effects)) {
        companion->EffectsChanged(!update);
        field->End();
    }
    if (changed) {
        if (gotItems) {
            controller->SetArmourModels(armourIds);
            controller->SetWeaponModels(weaponIds);
        }
        controller->ShowWeapon(activeWeapon);
        controller->RefreshEquipment();
    }
    return complete;
}

// The statistics field: the parameters read, the body part reactions and the hit reaction.
void Unit::ReadNetStats(DeltaReader& field, bool update, float hpBefore) {
    float before[6];
    for (int k = 0; k < 6; ++k) before[k] = StatFraction(this, k);
    field.ReadDelta(&params, sizeof params);
    // A dead unit skips the stat reactions.
    if (!IsDead() && update) {
        bool any = false;
        for (int k = 0; k < 6; ++k) {
            float now = StatFraction(this, k);
            if (Exceeds(now - before[k])) any = true;
            float change = before[k] - now;
            if (Exceeds(change)) controller->StatChanged(k, change, params.flags);
        }
        if (any) controller->RefreshWounds();
    } else if (!update) {
        controller->RefreshWounds();
    }
    float maxHp = static_cast<float>(packedVitals.maxHitPoints);
    float hpNow = static_cast<float>(packedVitals.hitPoints) / maxHp;
    // A drop (before > now, ordered), or UnitStatsFlag::Struck otherwise.
    bool dropped = hpBefore > hpNow;
    if ((dropped || (params.flags & UnitStatsFlag::Struck)) && update) {
        constexpr UnitStatsFlag kHitKinds = UnitStatsFlag::Backstab | UnitStatsFlag::Head | UnitStatsFlag::Electrical;
        std::uint32_t kind = params.flags & kHitKinds;
        float lost = hpBefore - hpNow;
        int damage = static_cast<int>(static_cast<std::int64_t>(lost * maxHp));
        companion->Hit(damage, static_cast<int>(kind));
        if (kind & UnitStatsFlag::Electrical) controller->Wound();
    }
}

// The path field: the order's state, then the client's motion built over it.
void Unit::ReadNetPath(DeltaReader& field) {
    PathOrder* path = controller->Path();
    path->ReadState(field);
    if (pathTimeShift == 1) path->ShiftTime(-1);
    path->ForgetObject(id);
    const MoveSpeeds speeds = GetMoveSpeeds();
    motion.segment = -1;
    motion.speedScale = speeds.forward;
    motion.baseTurnRate = speeds.turn;
    motion.Build(path);
    MarkChanged(NetField::Path);
    controller->MotionChanged();
}

// The relations field: the hostility mask, the side and the owning player. This client takes a player only when
// it is its view's own (any in a local session) and drops it when the server sends none.
void Unit::ReadNetRelations(DeltaReader& field) {
    field.ReadDelta(&relationsA, 4);
    side = 0;
    field.ReadDelta(&side, 1);
    int objectId;
    field.ReadDelta(&objectId, 4);
    auto setPlayer = [this](Player* newPlayer) {
        Player* old = player;
        player = newPlayer;
        if (old) old->RemoveUnit(this);
        if (Player* now = player) now->AddUnit(this);
        MarkChanged(NetField::Relations);
        if (controller) controller->PlayerChanged(newPlayer);
    };
    if (objectId == -1) {
        if (player) setPlayer(nullptr);
    } else if (!player || player->id != objectId) {
        PlayerClient* view = ClientWorld()->LocalView();
        Player* local = view->player;
        if ((local->id == objectId || view->localSession) && player != local) setPlayer(local);
    }
}
