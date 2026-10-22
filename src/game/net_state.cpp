// The network state writers of map objects (0050EAF0) and units (0052EC70): the virtual slot
// that writes an object's dirty fields into its DeltaState (+0x1C4) for the clients. See
// docs/notes/units.md ("Network state"). The slots are virtual; their bodies are the *Impl members,
// registered by member pointer (rewrite-guide rule 4).
#include "map_object_figures.h"
#include "net_delta.h"

#include "ai_map.h"
#include "ai_motion.h"
#include "map_object.h"
#include "player_client.h"
#include "player_messages.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>
#include <cstring>

namespace {

// The world (ObjectWorld) as the base class.
CWorld* WorldOf(const CEngineObject* object) { return ObjectWorld(object); }

// The unit's equipment ids (11 words: 4, the weapon, 6) [0052EC10 (out)]; the path's writer
// [005C7DB0 on the controller's path (field)].

constexpr int kClassServerController = 0x3E;

void PutByte(DeltaField* field, std::uint8_t value) { field->WriteDelta(&value, 1); }
void PutWord(DeltaField* field, std::uint32_t value) { field->WriteDelta(&value, 4); }

} // namespace

void CMapObject::WriteNetStateImpl() {
    DeltaState* state = NetState();   // +0x1C4
    if (DeltaField* field = state->BeginField(0)) {
        field->WriteDelta(&packedVitals, 0x18);   // 00523700
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(1)) {
        field->WriteDelta(&scale, 0xC);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        PutByte(field, static_cast<std::uint8_t>((flags_ >> 3) & 1));
        field->WriteDelta(&localPosition_.x, 0xC);
        field->WriteDelta(&localRotation_, 0x10);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        PutByte(field, static_cast<std::uint8_t>(effectCount));
        for (int i = 0; i < effectCount; ++i) {
            MapEffect* effect = &effects[i];
            field->WriteBoth(&effect->type, 1);
            field->WriteBoth(&effect->power, 1);
            field->WriteBoth(&effect->ticksLeft, 2);
            field->WriteBoth(&effect->ending, 1);
        }
        field->EndField();
    }
}

void CUnit::WriteNetStateImpl() {
    CMapObject::WriteNetStateImpl();
    CUnit* unit = this;
    DeltaState* state = NetState();
    if (DeltaField* field = state->BeginField(4)) {
        field->WriteDelta(unit->Params(), 0x108);   // 00523720
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        std::uint32_t ids[11];
        unit->GetEquipmentIds(ids);
        for (int i = 0; i < 4; ++i) PutWord(field, ids[i]);
        PutWord(field, (unit->flags_ & 0x40000000) ? 0xFFFFFFFFu : ids[4]);
        for (int i = 5; i < 11; ++i) PutWord(field, ids[i]);
        field->EndField();
        if (unit->controller->GetClassId() == kClassServerController)
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
        CPlayer* target = unit->player;
        std::uint32_t id = target ? static_cast<std::uint32_t>(target->id_) : 0xFFFFFFFFu;
        PutWord(field, unit->relationsB & unit->relationsA);
        PutByte(field, static_cast<std::uint8_t>(unit->side));
        PutWord(field, id);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(12)) {
        field->WriteDelta(unit->levels4C8, 0x14);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(13)) {
        field->WriteDelta(&unit->fightAtWill, 4);
        field->EndField();
    }
}

// ---- Readers ---------------------------------------------------------------------------------------

namespace {


// A companion (+0x170) as its virtual interface.

// Whether the stream read against (delta::g_readSnapshot) is consumed: slot 5 (position) == slot 4 (size).
std::uint32_t SnapshotConsumed() {
    BitStream* snapshot = delta::g_readSnapshot;
    std::uint32_t position = snapshot->Position();
    std::uint32_t size = delta::g_readSnapshot->Size();
    return position == size ? 1u : 0u;
}

} // namespace

std::uint32_t CMapObject::ReadNetStateImpl(int first) {
    DeltaState* state = NetState();
    std::uint32_t complete = 1;
    if (DeltaField* field = state->BeginField(0)) {
        short before = PackedVitals()->hitPoints;   // +0x17E
        field->ReadDelta(&packedVitals, 0x18);   // 00523710
        complete = SnapshotConsumed();
        field->EndField();
        if (before != PackedVitals()->hitPoints || first == 1) RefreshWounds();
    }
    if (DeltaField* field = state->BeginField(1)) {
        Vector3 figureScale;   // the figure scale (00517700)
        field->ReadDelta(&figureScale, 0xC);
        SetFigureScale(&figureScale.x);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        char hidden;
        field->ReadDelta(&hidden, 1);
        if (hidden)
            flags_ |= 8;
        else
            flags_ &= ~8u;
        Vector3 position;
        field->ReadDelta(&position, 0xC);
        field->ReadDelta(&localRotation_, 0x10);
        CWorld* world = WorldOf(this);
        world->MoveObject(this, &position.x);
        PlaceOnGround(localPosition_.z);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    DeltaField* field = state->BeginField(3);
    if (!field) return complete;
    std::uint32_t countWord = 0;   // (one byte read into a word whose upper bytes the original leaves)
    field->ReadDelta(&countWord, 1);
    int count = static_cast<int>(countWord & 0xFF);
    SetEffectCount(count);
    for (int i = 0; i < effectCount; ++i) {
        MapEffect* effect = &effects[i];
        effect->ticksLeft = 0;
        field->ReadRaw(&effect->type, 1);
        field->ReadRaw(&effect->power, 1);
        field->ReadRaw(&effect->ticksLeft, 2);
        field->ReadRaw(&effect->ending, 1);
    }
    if (GetClassId() != world::kClassUnit)
        companion->EffectsChanged(first == 0 ? 1 : 0);
    complete &= SnapshotConsumed();
    field->EndField();
    return complete;
}

namespace {

// CUnit helpers: release all items [005315B0], the move speeds [0052F920 (out, out)], MarkChanged
// [0050E9F0 (part)]; the path's reader [005C7FC0 (field)], 005C7CB0 (-1), 005C7C90 (unit id);
// the motion's 005C67D0 (path) on +0x27C; a player's unit list remove / add [00665B50 / 00665A70
// (unit)]; the object's manager context [00455940] and the client's local view [005699F0]; the
// field reader's CString [0047E310 (out)], CString operator= [00700AA9] and destructor [00700970].

constexpr float kStatEpsilon = 1e-4f;            // 0073CD44

// A body part's condition as current / maximum (signed 16-bit over unsigned 16-bit), as a float.
float StatFraction(CUnit* unit, int k) {
    const UnitParamsSlot& part = unit->params.slots[k];   // +0x378 + k * 0x1C: current, maximum
    return static_cast<float>(static_cast<double>(part.current) / part.maximum);
}

// fabs(v) > epsilon (FCOMP + TEST AH,0x41).
bool Exceeds(float v) {
    float a = v < 0 ? -v : v;
    return a > kStatEpsilon;
}

} // namespace

std::uint32_t CUnit::ReadNetStateImpl(int first) {
    CUnit* unit = this;
    float hpBefore = static_cast<float>(static_cast<double>(unit->packedVitals.hitPoints) / unit->packedVitals.maxHitPoints);
    std::uint32_t complete = CMapObject::ReadNetStateImpl(first);
    DeltaState* state = NetState();
    bool gotItems = false;
    bool changed = false;
    std::uint32_t ids4[4];
    std::uint32_t ids7[7];
    if (DeltaField* field = state->BeginField(4)) {
        float before[6];
        for (int k = 0; k < 6; ++k) before[k] = StatFraction(unit, k);
        field->ReadDelta(unit->Params(), 0x108);   // 00523730
        // Slot 55 (IsDead): a dead unit skips the stat reactions.
        if (!unit->IsDead() && first == 1) {
            bool any = false;
            for (int k = 0; k < 6; ++k) {
                float now = StatFraction(unit, k);
                if (Exceeds(now - before[k])) any = true;
                float change = before[k] - now;
                if (Exceeds(change)) {
                    unit->Controller()->StatChanged(k, change, unit->Params()->flags);
                }
            }
            if (any) unit->Controller()->RefreshWounds();
        } else if (first == 0) {
            unit->Controller()->RefreshWounds();
        }
        float maxHp = static_cast<float>(unit->packedVitals.maxHitPoints);
        float hpNow = static_cast<float>(unit->packedVitals.hitPoints) / maxHp;
        // FCOMP + TEST AH,0x41: a drop (before > now, ordered), or flag 0x10 otherwise.
        bool dropped = hpBefore > hpNow;
        if ((dropped || (unit->Params()->flags & 0x10)) && first != 0) {
            std::uint8_t flags = unit->Params()->flags;   // +0x437
            int kind = (flags & 2) ? 2 : 0;
            if (flags & 1) kind |= 1;
            if (flags & 4) kind |= 4;
            float lost = hpBefore - hpNow;
            int damage = static_cast<int>(static_cast<std::int64_t>(lost * maxHp));   // _ftol
            unit->companion->Hit(damage, kind);
            if (kind & 4) {
                unit->Controller()->Wound(lost);
            }
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        unit->ReleaseAllItems();
        for (auto& id : ids4) field->ReadDelta(&id, 4);
        for (auto& id : ids7) field->ReadDelta(&id, 4);
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
            unit->Controller()->ShowWeapon(value);   // slot 25
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
        float time = static_cast<float>(WorldOf(unit)->tick_);
        unit->Controller()->RestartAnimation(time);   // slot 14
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        {
            mfc::CString text(mfc::CString::kUnset);
            unit->scriptName = *field->ReadString(&text);
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        PathOrder* path = unit->Controller()->Path();
        path->ReadState(field);
        if (unit->pathTimeShift == 1) path->ShiftTime(-1);
        PathOrder* path2 = unit->Controller()->Path();
        path2->ForgetObject(unit->id_);
        float walk, run;
        unit->GetMoveSpeeds(&walk, &run);
        UnitMotion* walker = unit->Motion();   // +0x27C
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
        int id;
        field->ReadDelta(&id, 4);
        CPlayer* current = unit->player;
        if (id == -1) {
            if (current) {
                unit->player = nullptr;
                current->RemoveUnit(unit);
                if (CPlayer* now = unit->player) now->AddUnit(unit);
                unit->MarkChanged(11);
                if (unit->controller) unit->Controller()->PlayerChanged(nullptr);
            }
        } else if (!current || current->id_ != id) {
            CPlayerClient* view = unit->ClientWorld()->LocalView();
            CPlayer* local = view->player_;
            if ((local->id_ == id || view->localSession_) && unit->player != local) {
                CPlayer* old = unit->player;
                unit->player = local;
                if (old) old->RemoveUnit(unit);
                if (CPlayer* now = unit->player) now->AddUnit(unit);
                unit->MarkChanged(11);
                if (unit->controller) unit->Controller()->PlayerChanged(local);
            }
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(12)) {
        field->ReadDelta(unit->levels4C8, 0x14);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(13)) {
        field->ReadDelta(&unit->fightAtWill, 4);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        unit->companion->EffectsChanged(first == 0 ? 1 : 0);
        field->EndField();
    }
    if (changed) {
        if (gotItems) {
            unit->Controller()->SetArmourModels(ids7, 7);
            unit->Controller()->SetWeaponModels(ids4, 4);
        }
        unit->Controller()->ShowWeapon(unit->activeWeapon);   // slot 25
        unit->Controller()->RefreshEquipment();
    }
    return complete;
}

EI_REWRITTEN(0x0052EF90, CUnit::ReadNetStateImpl);
EI_REWRITTEN(0x0050EC30, CMapObject::ReadNetStateImpl);
EI_REWRITTEN(0x0050EAF0, CMapObject::WriteNetStateImpl);
EI_REWRITTEN(0x0052EC70, CUnit::WriteNetStateImpl);
