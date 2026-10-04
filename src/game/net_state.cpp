// The network state writers of map objects (0050EAF0) and units (0052EC70): the virtual slot
// that writes an object's dirty fields into its DeltaState (+0x1C4) for the clients. See
// docs/notes/units.md ("Network state"). They are registered as free functions (thiscall as
// fastcall): the slots are virtual (rewrite-guide rule 4).
#include "net_delta.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

DeltaState* StateOf(std::uint8_t* object) { return reinterpret_cast<DeltaState*>(object + 0x1C4); }

// The unit's equipment ids (11 words: 4, the weapon, 6) [0052EC10 (out)]; the path's writer
// [005C7DB0 on the controller's path (field)].
__declspec(naked) void __fastcall EquipmentIds(void*, void*, std::uint32_t*) EI_ORIGINAL(0x0052EC10)
__declspec(naked) void __fastcall WritePath(void*, void*, DeltaField*) EI_ORIGINAL(0x005C7DB0)

constexpr int kSlotClassId = 0x30 / 4;         // controller: ()
constexpr int kSlotRefreshItems = 0x74 / 4;    // controller of class 0x3E: ()
constexpr int kSlotPath = 0x40 / 4;            // controller: () -> its path
constexpr int kClassServerController = 0x3E;

void PutByte(DeltaField* field, std::uint8_t value) { field->WriteDelta(&value, 1); }
void PutWord(DeltaField* field, std::uint32_t value) { field->WriteDelta(&value, 4); }

} // namespace

void __fastcall MapObjectWriteNetState(std::uint8_t* object) {
    DeltaState* state = StateOf(object);
    if (DeltaField* field = state->BeginField(0)) {
        field->WriteDelta(object + 0x17C, 0x18);   // 00523700
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(1)) {
        field->WriteDelta(object + 0xC4, 0xC);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(2)) {
        PutByte(field, static_cast<std::uint8_t>((At<std::uint32_t>(object, 8) >> 3) & 1));
        field->WriteDelta(object + 0x1C, 0xC);
        field->WriteDelta(object + 0x34, 0x10);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        PutByte(field, At<std::uint8_t>(object, 0x19C));
        for (int i = 0; i < At<int>(object, 0x19C); ++i) {
            std::uint8_t* effect = At<std::uint8_t*>(object, 0x198) + i * 0x14;
            field->WriteBoth(effect, 1);
            field->WriteBoth(effect + 1, 1);
            field->WriteBoth(effect + 4, 2);
            field->WriteBoth(effect + 0x10, 1);
        }
        field->EndField();
    }
}

void __fastcall UnitWriteNetState(std::uint8_t* unit) {
    MapObjectWriteNetState(unit);
    DeltaState* state = StateOf(unit);
    if (DeltaField* field = state->BeginField(4)) {
        field->WriteDelta(unit + 0x330, 0x108);   // 00523720
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(5)) {
        std::uint32_t ids[11];
        EquipmentIds(unit, nullptr, ids);
        for (int i = 0; i < 4; ++i) PutWord(field, ids[i]);
        PutWord(field, (At<std::uint32_t>(unit, 8) & 0x40000000) ? 0xFFFFFFFFu : ids[4]);
        for (int i = 5; i < 11; ++i) PutWord(field, ids[i]);
        field->EndField();
        if (ei::VCall<int>(At<void*>(unit, 0x240), kSlotClassId) == kClassServerController)
            ei::VCall<int>(At<void*>(unit, 0x240), kSlotRefreshItems);
    }
    if (DeltaField* field = state->BeginField(6)) {
        field->WriteDelta(unit + 0x264, 4);
        field->WriteDelta(unit + 0x268, 4);
        PutWord(field, At<std::uint32_t>(unit, 0x26C));
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(7)) {
        std::uint8_t bits = static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>((static_cast<std::uint8_t>(At<std::uint8_t>(unit, 0x278) << 1) |
                                        (At<std::uint8_t>(unit, 0x274) & 1))
                                       << 3) |
             (At<std::uint8_t>(unit, 0x260) & 7))
            << 3);
        bits = static_cast<std::uint8_t>(bits | (At<std::uint8_t>(unit, 0x25C) & 7));
        PutByte(field, bits);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(8)) {
        PutByte(field, At<std::uint8_t>(unit, 0x254));
        PutByte(field, At<std::uint8_t>(unit, 0x255));
        PutWord(field, At<std::uint32_t>(unit, 0x258));
        PutByte(field, At<std::uint8_t>(unit, 0x256));
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        field->WriteStringBoth(At<const char*>(unit, 0x238));
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        void* path = ei::VCall<void*>(At<void*>(unit, 0x240), kSlotPath);
        WritePath(path, nullptr, field);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(11)) {
        void* target = At<void*>(unit, 0x24C);
        std::uint32_t id = target ? At<std::uint32_t>(target, 0xC) : 0xFFFFFFFFu;
        PutWord(field, At<std::uint32_t>(unit, 0x248) & At<std::uint32_t>(unit, 0x244));
        PutByte(field, At<std::uint8_t>(unit, 0x250));
        PutWord(field, id);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(12)) {
        field->WriteDelta(unit + 0x4C8, 0x14);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(13)) {
        field->WriteDelta(unit + 0x438, 4);
        field->EndField();
    }
}

// ---- Readers ---------------------------------------------------------------------------------------

namespace {

// The object's position setter [00517700], its world [00455950], the effect array's
// CArray::SetSize [0050FE70, on +0x194 (size, grow by)].
__declspec(naked) void __fastcall SetPosition(void*, void*, const float*) EI_ORIGINAL(0x00517700)
__declspec(naked) std::uint8_t* __fastcall ObjectWorld(void*) EI_ORIGINAL(0x00455950)
__declspec(naked) void __fastcall EffectsSetSize(void*, void*, int, int) EI_ORIGINAL(0x0050FE70)

constexpr int kSlotAppearanceChanged = 0xE8 / 4;   // object: ()
constexpr int kSlotMoveObject = 0xA4 / 4;          // world: (object, position)
constexpr int kSlotSetHeading = 0xCC / 4;          // object: (+0x24)
constexpr int kSlotObjectClass = 0x30 / 4;         // object: ()
constexpr int kSlotEffectsChanged = 0x34 / 4;      // companion (+0x170): (first)
constexpr int kStreamPosition = 5;
constexpr int kStreamSize = 4;
constexpr int kClassUnit = 0x50;

// Whether the stream read against (007A9794) is consumed: slot 5 (position) == slot 4 (size).
std::uint32_t SnapshotConsumed() {
    BitStream* snapshot = delta::ReadSnapshot();
    std::uint32_t position = ei::VCall<std::uint32_t>(snapshot, kStreamPosition);
    std::uint32_t size = ei::VCall<std::uint32_t>(delta::ReadSnapshot(), kStreamSize);
    return position == size ? 1u : 0u;
}

} // namespace

std::uint32_t __fastcall MapObjectReadNetState(std::uint8_t* object, void*, int first) {
    DeltaState* state = StateOf(object);
    std::uint32_t complete = 1;
    if (DeltaField* field = state->BeginField(0)) {
        short before = At<short>(object, 0x17E);
        field->ReadDelta(object + 0x17C, 0x18);   // 00523710
        complete = SnapshotConsumed();
        field->EndField();
        if (before != At<short>(object, 0x17E) || first == 1) ei::VCall<void>(object, kSlotAppearanceChanged);
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
            At<std::uint32_t>(object, 8) |= 8;
        else
            At<std::uint32_t>(object, 8) &= ~8u;
        float position[3];
        field->ReadDelta(position, 0xC);
        field->ReadDelta(object + 0x34, 0x10);
        std::uint8_t* world = ObjectWorld(object);
        ei::VCall<void>(world, kSlotMoveObject, object, position);
        ei::VCall<void>(object, kSlotSetHeading, At<std::uint32_t>(object, 0x24));
        complete &= SnapshotConsumed();
        field->EndField();
    }
    DeltaField* field = state->BeginField(3);
    if (!field) return complete;
    std::uint32_t countWord = 0;   // (one byte read into a word whose upper bytes the original leaves)
    field->ReadDelta(&countWord, 1);
    int count = static_cast<int>(countWord & 0xFF);
    EffectsSetSize(object + 0x194, nullptr, count, -1);
    for (int i = 0; i < At<int>(object, 0x19C); ++i) {
        std::uint8_t* effect = At<std::uint8_t*>(object, 0x198) + i * 0x14;
        At<std::uint32_t>(effect, 4) = 0;
        field->ReadRaw(effect, 1);
        field->ReadRaw(effect + 1, 1);
        field->ReadRaw(effect + 4, 2);
        field->ReadRaw(effect + 0x10, 1);
    }
    if (ei::VCall<int>(object, kSlotObjectClass) != kClassUnit)
        ei::VCall<void>(At<void*>(object, 0x170), kSlotEffectsChanged, first == 0 ? 1 : 0);
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
__declspec(naked) void __fastcall UnitReleaseAllItems(void*) EI_ORIGINAL(0x005315B0)
__declspec(naked) void __fastcall UnitMoveSpeeds(void*, void*, float*, float*) EI_ORIGINAL(0x0052F920)
__declspec(naked) void __fastcall UnitMarkChanged(void*, void*, int) EI_ORIGINAL(0x0050E9F0)
__declspec(naked) void __fastcall PathRead(void*, void*, DeltaField*) EI_ORIGINAL(0x005C7FC0)
__declspec(naked) void __fastcall Path7CB0(void*, void*, int) EI_ORIGINAL(0x005C7CB0)
__declspec(naked) void __fastcall PathSetOwner(void*, void*, int) EI_ORIGINAL(0x005C7C90)
__declspec(naked) void __fastcall Motion67D0(void*, void*, void*) EI_ORIGINAL(0x005C67D0)
__declspec(naked) void __fastcall PlayerRemoveUnit(void*, void*, void*) EI_ORIGINAL(0x00665B50)
__declspec(naked) void __fastcall PlayerAddUnit(void*, void*, void*) EI_ORIGINAL(0x00665A70)
__declspec(naked) void* __fastcall ManagerContext(void*) EI_ORIGINAL(0x00455940)
__declspec(naked) std::uint8_t* __fastcall LocalView(void*) EI_ORIGINAL(0x005699F0)
__declspec(naked) void* __fastcall FieldReadString(DeltaField*, void*, void*) EI_ORIGINAL(0x0047E310)
__declspec(naked) void __fastcall CStringAssign(void*, void*, const void*) EI_ORIGINAL(0x00700AA9)
__declspec(naked) void __fastcall CStringDestroy(void*) EI_ORIGINAL(0x00700970)

constexpr int kSlotIsRemote = 0xDC / 4;          // unit: () -> non-zero skips the stat reactions
constexpr int kSlotStatChanged = 0xB8 / 4;       // controller: (stat, change, flags)
constexpr int kSlotStatsChanged = 0x7C / 4;      // controller: ()
constexpr int kSlotHit = 0x40 / 4;               // companion: (damage, kind)
constexpr int kSlotWound = 0xBC / 4;             // controller: (fraction lost)
constexpr int kSlotSetSpeedMode = 0x64 / 4;      // controller: (value)
constexpr int kSlotSetTime = 0x38 / 4;           // controller: (game time)
constexpr int kSlotMotionChanged = 0xA4 / 4;     // controller: ()
constexpr int kSlotSetPlayer = 0x48 / 4;         // controller: (player)
constexpr int kSlotSetItems = 0x80 / 4;          // controller: (ids, 7)
constexpr int kSlotSetEquipment = 0x84 / 4;      // controller: (ids, 4)
constexpr int kSlotItemsChanged = 0x74 / 4;      // controller: ()
constexpr float kStatEpsilon = 1e-4f;            // 0073CD44

// A stat as current / maximum (signed 16-bit over unsigned 16-bit), as a float.
float StatFraction(std::uint8_t* unit, int k) {
    std::uint8_t* stat = unit + 0x378 + k * 0x1C;
    return static_cast<float>(static_cast<double>(At<short>(stat, 0)) / At<std::uint16_t>(stat, 2));
}

// fabs(v) > epsilon with an ordered compare (FCOMP + TEST AH,0x41: a NaN is not greater).
bool Exceeds(float v) {
    float a = v < 0 ? -v : v;
    return !x87::IsNaN(a) && a > kStatEpsilon;
}

} // namespace

std::uint32_t __fastcall UnitReadNetState(std::uint8_t* unit, void*, int first) {
    float hpBefore = static_cast<float>(static_cast<double>(At<short>(unit, 0x17E)) / At<short>(unit, 0x180));
    std::uint32_t complete = MapObjectReadNetState(unit, nullptr, first);
    DeltaState* state = StateOf(unit);
    bool gotItems = false;
    bool changed = false;
    std::uint32_t ids4[4];
    std::uint32_t ids7[7];
    if (DeltaField* field = state->BeginField(4)) {
        float before[6];
        for (int k = 0; k < 6; ++k) before[k] = StatFraction(unit, k);
        field->ReadDelta(unit + 0x330, 0x108);   // 00523730
        if (!ei::VCall<int>(unit, kSlotIsRemote) && first == 1) {
            bool any = false;
            for (int k = 0; k < 6; ++k) {
                float now = StatFraction(unit, k);
                if (Exceeds(now - before[k])) any = true;
                float change = before[k] - now;
                if (Exceeds(change)) {
                    std::uint32_t changeBits;
                    std::memcpy(&changeBits, &change, 4);
                    ei::VCall<void>(At<void*>(unit, 0x240), kSlotStatChanged, k, changeBits,
                                    static_cast<std::uint32_t>(At<std::uint8_t>(unit, 0x437)));
                }
            }
            if (any) ei::VCall<void>(At<void*>(unit, 0x240), kSlotStatsChanged);
        } else if (first == 0) {
            ei::VCall<void>(At<void*>(unit, 0x240), kSlotStatsChanged);
        }
        float maxHp = static_cast<float>(At<short>(unit, 0x180));
        float hpNow = static_cast<float>(At<short>(unit, 0x17E)) / maxHp;
        // FCOMP + TEST AH,0x41: a drop (before > now, ordered), or flag 0x10 otherwise.
        bool dropped = !x87::IsNaN(hpBefore) && !x87::IsNaN(hpNow) && hpBefore > hpNow;
        if ((dropped || (At<std::uint8_t>(unit, 0x437) & 0x10)) && first != 0) {
            std::uint8_t flags = At<std::uint8_t>(unit, 0x437);
            int kind = (flags & 2) ? 2 : 0;
            if (flags & 1) kind |= 1;
            if (flags & 4) kind |= 4;
            float lost = hpBefore - hpNow;
            int damage = static_cast<int>(static_cast<std::int64_t>(lost * maxHp));   // _ftol
            ei::VCall<void>(At<void*>(unit, 0x170), kSlotHit, damage, kind);
            if (kind & 4) {
                std::uint32_t lostBits;
                std::memcpy(&lostBits, &lost, 4);
                ei::VCall<void>(At<void*>(unit, 0x240), kSlotWound, lostBits);
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
        field->ReadDelta(unit + 0x264, 4);
        field->ReadDelta(unit + 0x268, 4);
        std::uint32_t value;
        field->ReadDelta(&value, 4);
        if (At<std::uint32_t>(unit, 0x26C) != value) {
            changed = true;
            ei::VCall<void>(At<void*>(unit, 0x240), kSlotSetSpeedMode, value);
        }
        At<std::uint32_t>(unit, 0x270) = At<std::uint32_t>(unit, 0x26C);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(7)) {
        std::uint8_t bits;
        field->ReadDelta(&bits, 1);
        At<int>(unit, 0x25C) = static_cast<signed char>(bits << 5) >> 5;
        At<int>(unit, 0x260) = static_cast<signed char>(bits << 2) >> 5;
        At<int>(unit, 0x274) = static_cast<signed char>(bits << 1) >> 7;
        At<int>(unit, 0x278) = static_cast<signed char>(bits) >> 7;
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(8)) {
        field->ReadDelta(unit + 0x254, 1);
        field->ReadDelta(unit + 0x255, 1);
        field->ReadDelta(unit + 0x258, 4);
        field->ReadDelta(unit + 0x256, 1);
        if (At<int>(unit, 0x25C) == 1) --At<int>(unit, 0x258);
        float time = static_cast<float>(At<int>(ObjectWorld(unit), 0x64));
        std::uint32_t timeBits;
        std::memcpy(&timeBits, &time, 4);
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotSetTime, timeBits);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(9)) {
        void* text;
        void* read = FieldReadString(field, nullptr, &text);
        CStringAssign(unit + 0x238, nullptr, read);
        CStringDestroy(&text);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(10)) {
        void* path = ei::VCall<void*>(At<void*>(unit, 0x240), kSlotPath);
        PathRead(path, nullptr, field);
        if (At<int>(unit, 0x260) == 1) Path7CB0(path, nullptr, -1);
        void* path2 = ei::VCall<void*>(At<void*>(unit, 0x240), kSlotPath);
        PathSetOwner(path2, nullptr, At<int>(unit, 0xC));
        float walk, run;
        UnitMoveSpeeds(unit, nullptr, &walk, &run);
        At<int>(unit, 0x27C + 0xA0) = -1;
        At<float>(unit, 0x27C + 0x3C) = walk;
        At<float>(unit, 0x27C + 0x40) = run;
        Motion67D0(unit + 0x27C, nullptr, path2);
        UnitMarkChanged(unit, nullptr, 10);
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotMotionChanged);
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(11)) {
        field->ReadDelta(unit + 0x244, 4);
        At<std::uint32_t>(unit, 0x250) = 0;
        field->ReadDelta(unit + 0x250, 1);
        int id;
        field->ReadDelta(&id, 4);
        void* current = At<void*>(unit, 0x24C);
        if (id == -1) {
            if (current) {
                At<void*>(unit, 0x24C) = nullptr;
                PlayerRemoveUnit(current, nullptr, unit);
                if (void* now = At<void*>(unit, 0x24C)) PlayerAddUnit(now, nullptr, unit);
                UnitMarkChanged(unit, nullptr, 11);
                if (void* c = At<void*>(unit, 0x240)) ei::VCall<void>(c, kSlotSetPlayer, nullptr);
            }
        } else if (!current || At<int>(current, 0xC) != id) {
            std::uint8_t* view = LocalView(ManagerContext(unit));
            void* local = At<void*>(view, 0x1C);
            if ((At<int>(local, 0xC) == id || At<std::uint8_t>(view, 0x84)) && At<void*>(unit, 0x24C) != local) {
                void* old = At<void*>(unit, 0x24C);
                At<void*>(unit, 0x24C) = local;
                if (old) PlayerRemoveUnit(old, nullptr, unit);
                if (void* now = At<void*>(unit, 0x24C)) PlayerAddUnit(now, nullptr, unit);
                UnitMarkChanged(unit, nullptr, 11);
                if (void* c = At<void*>(unit, 0x240)) ei::VCall<void>(c, kSlotSetPlayer, local);
            }
        }
        complete &= SnapshotConsumed();
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(12)) {
        field->ReadDelta(unit + 0x4C8, 0x14);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(13)) {
        field->ReadDelta(unit + 0x438, 4);
        field->EndField();
    }
    if (DeltaField* field = state->BeginField(3)) {
        ei::VCall<void>(At<void*>(unit, 0x170), kSlotEffectsChanged, first == 0 ? 1 : 0);
        field->EndField();
    }
    if (changed) {
        if (gotItems) {
            ei::VCall<void>(At<void*>(unit, 0x240), kSlotSetItems, ids7, 7);
            ei::VCall<void>(At<void*>(unit, 0x240), kSlotSetEquipment, ids4, 4);
        }
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotSetSpeedMode, At<std::uint32_t>(unit, 0x26C));
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotItemsChanged);
    }
    return complete;
}

EI_REPLACE(0x0052EF90, UnitReadNetState);
EI_REPLACE(0x0050EC30, MapObjectReadNetState);
EI_REPLACE(0x0050EAF0, MapObjectWriteNetState);
EI_REPLACE(0x0052EC70, UnitWriteNetState);
