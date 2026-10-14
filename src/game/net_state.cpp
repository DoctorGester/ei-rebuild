// The network state writers of map objects (0050EAF0) and units (0052EC70): the virtual slot
// that writes an object's dirty fields into its DeltaState (+0x1C4) for the clients. See
// docs/notes/units.md ("Network state"). They are registered as free functions (thiscall as
// fastcall): the slots are virtual (rewrite-guide rule 4).
#include "net_delta.h"

#include "ai_map.h"
#include "ai_motion.h"
#include "map_object.h"
#include "player_messages.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"

#include <cstdint>
#include <cstring>

namespace {

CMapObject* AsMapObject(std::uint8_t* object) { return reinterpret_cast<CMapObject*>(object); }
CUnit* AsUnit(std::uint8_t* unit) { return reinterpret_cast<CUnit*>(unit); }
DeltaState* StateOf(std::uint8_t* object) { return AsMapObject(object)->NetState(); }   // +0x1C4
// The world (ObjectWorld) as the base class.
CWorld* WorldOf(const void* object) { return static_cast<CWorld*>(ObjectWorld(object)); }
// The unit's player (CUnit::player, a CPlayer).
CPlayer* AsPlayer(void* player) { return static_cast<CPlayer*>(player); }

// The unit's equipment ids (11 words: 4, the weapon, 6) [0052EC10 (out)]; the path's writer
// [005C7DB0 on the controller's path (field)].
__declspec(naked) void __fastcall EquipmentIds(void*, void*, std::uint32_t*) EI_ORIGINAL(0x0052EC10)
// (005C7DB0: PathWriteOriginal, originals_ai.h.)

constexpr int kClassServerController = 0x3E;

void PutByte(DeltaField* field, std::uint8_t value) { field->WriteDelta(&value, 1); }
void PutWord(DeltaField* field, std::uint32_t value) { field->WriteDelta(&value, 4); }

} // namespace

void __fastcall MapObjectWriteNetState(std::uint8_t* object) {
    CMapObject* self = AsMapObject(object);
    DeltaState* state = StateOf(object);
    if (DeltaField* field = state->BeginField(0)) {
        field->WriteDelta(self->packedVitals, 0x18);   // 00523700
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(1)) {
        field->WriteDelta(&self->scale, 0xC);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        PutByte(field, static_cast<std::uint8_t>((self->flags_ >> 3) & 1));
        field->WriteDelta(&self->position.x, 0xC);
        field->WriteDelta(&self->orientation, 0x10);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        PutByte(field, static_cast<std::uint8_t>(self->effectCount));
        for (int i = 0; i < self->effectCount; ++i) {
            MapEffect* effect = &self->effects[i];
            field->WriteBoth(&effect->type, 1);
            field->WriteBoth(&effect->power, 1);
            field->WriteBoth(&effect->ticksLeft, 2);
            field->WriteBoth(&effect->ending, 1);
        }
        field->EndField();
    }
}

void __fastcall UnitWriteNetState(std::uint8_t* object) {
    MapObjectWriteNetState(object);
    CUnit* unit = AsUnit(object);
    DeltaState* state = StateOf(object);
    if (DeltaField* field = state->BeginField(4)) {
        field->WriteDelta(unit->Params(), 0x108);   // 00523720
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        std::uint32_t ids[11];
        EquipmentIds(unit, nullptr, ids);
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
            (static_cast<std::uint8_t>((static_cast<std::uint8_t>(static_cast<std::uint8_t>(unit->unknown_278) << 1) |
                                        (static_cast<std::uint8_t>(unit->dead) & 1))
                                       << 3) |
             (static_cast<std::uint8_t>(unit->unknown_260) & 7))
            << 3);
        bits = static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(unit->unknown_25C) & 7));
        PutByte(field, bits);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(8)) {
        PutByte(field, static_cast<std::uint8_t>(unit->unknown_254));
        PutByte(field, static_cast<std::uint8_t>(unit->unknown_255));
        PutWord(field, static_cast<std::uint32_t>(unit->unknown_258));
        PutByte(field, static_cast<std::uint8_t>(unit->unknown_256));
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        field->WriteStringBoth(unit->scriptName);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        AIPath* path = unit->Controller()->Path();
        PathWriteOriginal(path, nullptr, field);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(11)) {
        char* target = unit->player;
        std::uint32_t id = target ? static_cast<std::uint32_t>(AsPlayer(target)->id_) : 0xFFFFFFFFu;
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
        field->WriteDelta(&unit->unknown_438, 4);
        field->EndField();
    }
}

// ---- Readers ---------------------------------------------------------------------------------------

namespace {

// The object's position setter [00517700], its world [00455950], the effect array's
// CArray::SetSize [0050FE70, on +0x194 (size, grow by)].
__declspec(naked) void __fastcall SetPosition(void*, void*, const float*) EI_ORIGINAL(0x00517700)
void __fastcall EffectsSetSize(void* a0, void*, int a2, int a3) { EffectArraySetSizeOriginal(a0, nullptr, a2, a3); }

// A companion (+0x170) as its virtual interface.
CMapObjectCompanion* CompanionOf(CEngineObject* companion) { return static_cast<CMapObjectCompanion*>(companion); }

// Whether the stream read against (007A9794) is consumed: slot 5 (position) == slot 4 (size).
std::uint32_t SnapshotConsumed() {
    BitStream* snapshot = delta::ReadSnapshot();
    std::uint32_t position = snapshot->Position();
    std::uint32_t size = delta::ReadSnapshot()->Size();
    return position == size ? 1u : 0u;
}

} // namespace

std::uint32_t __fastcall MapObjectReadNetState(std::uint8_t* object, void*, int first) {
    CMapObject* self = AsMapObject(object);
    DeltaState* state = StateOf(object);
    std::uint32_t complete = 1;
    if (DeltaField* field = state->BeginField(0)) {
        short before = self->PackedVitals()->hitPoints;   // +0x17E
        field->ReadDelta(self->packedVitals, 0x18);   // 00523710
        complete = SnapshotConsumed();
        field->EndField();
        if (before != self->PackedVitals()->hitPoints || first == 1) ObjectSlots(self)->RefreshWounds();
    }
    if (DeltaField* field = state->BeginField(1)) {
        float position[3];
        field->ReadDelta(position, 0xC);
        SetPosition(object, nullptr, position);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        char hidden;
        field->ReadDelta(&hidden, 1);
        if (hidden)
            self->flags_ |= 8;
        else
            self->flags_ &= ~8u;
        float position[3];
        field->ReadDelta(position, 0xC);
        field->ReadDelta(&self->orientation, 0x10);
        CWorld* world = WorldOf(object);
        world->MoveObject(self, position);
        ObjectSlots(self)->PlaceOnGround(self->position.z);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    DeltaField* field = state->BeginField(3);
    if (!field) return complete;
    std::uint32_t countWord = 0;   // (one byte read into a word whose upper bytes the original leaves)
    field->ReadDelta(&countWord, 1);
    int count = static_cast<int>(countWord & 0xFF);
    EffectsSetSize(&self->effectsVtable, nullptr, count, -1);
    for (int i = 0; i < self->effectCount; ++i) {
        MapEffect* effect = &self->effects[i];
        effect->ticksLeft = 0;
        field->ReadRaw(&effect->type, 1);
        field->ReadRaw(&effect->power, 1);
        field->ReadRaw(&effect->ticksLeft, 2);
        field->ReadRaw(&effect->ending, 1);
    }
    if (self->GetClassId() != world::kClassUnit)
        CompanionOf(self->companion)->EffectsChanged(first == 0 ? 1 : 0);
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
__declspec(naked) void __fastcall UnitMoveSpeeds(void*, void*, float*, float*) EI_ORIGINAL(0x0052F920)
// (005C7FC0: PathReadOriginal, originals_ai.h.)
__declspec(naked) void __fastcall PathSetOwner(void*, void*, int) EI_ORIGINAL(0x005C7C90)
void __fastcall PlayerRemoveUnit(void* a0, void*, void* a2) { PlayerRemoveUnitOriginal(a0, nullptr, a2); }
void __fastcall PlayerAddUnit(void* a0, void*, void* a2) { PlayerAddUnitOriginal(a0, nullptr, a2); }
__declspec(naked) std::uint8_t* __fastcall LocalView(void*) EI_ORIGINAL(0x005699F0)
void __fastcall CStringAssign(void* a0, void*, const void* a2) { CStringAssignOriginal(a0, nullptr, a2); }

constexpr float kStatEpsilon = 1e-4f;            // 0073CD44

// A body part's condition as current / maximum (signed 16-bit over unsigned 16-bit), as a float.
float StatFraction(CUnit* unit, int k) {
    const UnitBodyPart& part = unit->bodyParts[k];   // +0x378 + k * 0x1C: durability, maximum
    return static_cast<float>(static_cast<double>(part.durability) / part.maxDurability);
}

// fabs(v) > epsilon with an ordered compare (FCOMP + TEST AH,0x41: a NaN is not greater).
bool Exceeds(float v) {
    float a = v < 0 ? -v : v;
    return !x87::IsNaN(a) && a > kStatEpsilon;
}

} // namespace

std::uint32_t __fastcall UnitReadNetState(std::uint8_t* object, void*, int first) {
    CUnit* unit = AsUnit(object);
    float hpBefore = static_cast<float>(static_cast<double>(unit->hitPoints) / unit->maxHitPoints);
    std::uint32_t complete = MapObjectReadNetState(object, nullptr, first);
    DeltaState* state = StateOf(object);
    bool gotItems = false;
    bool changed = false;
    std::uint32_t ids4[4];
    std::uint32_t ids7[7];
    if (DeltaField* field = state->BeginField(4)) {
        float before[6];
        for (int k = 0; k < 6; ++k) before[k] = StatFraction(unit, k);
        field->ReadDelta(unit->Params(), 0x108);   // 00523730
        // Slot 55 (IsDead): a dead unit skips the stat reactions.
        if (!ObjectSlots(unit)->IsDead() && first == 1) {
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
        float maxHp = static_cast<float>(unit->maxHitPoints);
        float hpNow = static_cast<float>(unit->hitPoints) / maxHp;
        // FCOMP + TEST AH,0x41: a drop (before > now, ordered), or flag 0x10 otherwise.
        bool dropped = !x87::IsNaN(hpBefore) && !x87::IsNaN(hpNow) && hpBefore > hpNow;
        if ((dropped || (unit->Params()->flags & 0x10)) && first != 0) {
            std::uint8_t flags = unit->Params()->flags;   // +0x437
            int kind = (flags & 2) ? 2 : 0;
            if (flags & 1) kind |= 1;
            if (flags & 4) kind |= 4;
            float lost = hpBefore - hpNow;
            int damage = static_cast<int>(static_cast<std::int64_t>(lost * maxHp));   // _ftol
            CompanionOf(unit->companion)->Hit(damage, kind);
            if (kind & 4) {
                unit->Controller()->Wound(lost);
            }
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        UnitReleaseAllItems(unit);
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
        unit->unknown_270 = static_cast<int>(unit->activeWeapon);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(7)) {
        std::uint8_t bits;
        field->ReadDelta(&bits, 1);
        unit->unknown_25C = static_cast<signed char>(bits << 5) >> 5;
        unit->unknown_260 = static_cast<signed char>(bits << 2) >> 5;
        unit->dead = static_cast<signed char>(bits << 1) >> 7;
        unit->unknown_278 = static_cast<std::uint32_t>(static_cast<signed char>(bits) >> 7);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(8)) {
        field->ReadDelta(&unit->unknown_254, 1);
        field->ReadDelta(&unit->unknown_255, 1);
        field->ReadDelta(&unit->unknown_258, 4);
        field->ReadDelta(&unit->unknown_256, 1);
        if (unit->unknown_25C == 1) --unit->unknown_258;
        float time = static_cast<float>(WorldOf(unit)->tick_);
        unit->Controller()->RestartAnimation(time);   // slot 14
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        void* text;
        void* read = FieldReadString(field, nullptr, &text);
        CStringAssign(&unit->scriptName, nullptr, read);
        CStringDestroy(&text);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        AIPath* path = unit->Controller()->Path();
        PathReadOriginal(path, nullptr, field);
        if (unit->unknown_260 == 1) PathShiftTime(path, nullptr, -1);
        AIPath* path2 = unit->Controller()->Path();
        PathSetOwner(path2, nullptr, unit->id_);
        float walk, run;
        UnitMoveSpeeds(unit, nullptr, &walk, &run);
        UnitMotion* motion = unit->Motion();   // +0x27C
        motion->segment = -1;
        motion->speedScale = walk;
        motion->baseTurnRate = run;
        MotionBuild(motion, nullptr, path2);
        UnitMarkChanged(unit, nullptr, 10);
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
        char* current = unit->player;
        if (id == -1) {
            if (current) {
                unit->player = nullptr;
                PlayerRemoveUnit(current, nullptr, unit);
                if (char* now = unit->player) PlayerAddUnit(now, nullptr, unit);
                UnitMarkChanged(unit, nullptr, 11);
                if (unit->controller) unit->Controller()->PlayerChanged(nullptr);
            }
        } else if (!current || AsPlayer(current)->id_ != id) {
            auto* view = reinterpret_cast<LocalViewData*>(LocalView(ManagerContextOf(unit)));
            char* local = reinterpret_cast<char*>(view->player);
            if ((AsPlayer(local)->id_ == id || view->unknown_84) && unit->player != local) {
                char* old = unit->player;
                unit->player = local;
                if (old) PlayerRemoveUnit(old, nullptr, unit);
                if (char* now = unit->player) PlayerAddUnit(now, nullptr, unit);
                UnitMarkChanged(unit, nullptr, 11);
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
        field->ReadDelta(&unit->unknown_438, 4);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        CompanionOf(unit->companion)->EffectsChanged(first == 0 ? 1 : 0);
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

EI_REPLACE(0x0052EF90, UnitReadNetState);
EI_REPLACE(0x0050EC30, MapObjectReadNetState);
EI_REPLACE(0x0050EAF0, MapObjectWriteNetState);
EI_REPLACE(0x0052EC70, UnitWriteNetState);
