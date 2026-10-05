// CWorldClient: terrain and platform height queries, unit and exit lookups. See world.h.
#include "world.h"

#include "core_maths.h"
#include "terrain.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_engine.h"
#include "originals_world.h"

#include <cstring>

namespace {

constexpr int kSlotUnitIsDead = 55;     // units: +0xDC, non-zero when dead
constexpr int kSlotControllerBusy = 5;  // player state controller: +0x14

// 0055FB00 (world base): FindObject (slot 40) restricted to units.
struct WorldBase {
    CEngineObject* FindUnit(unsigned id);
    BOOL IsNight();   // 0055FC10: hour of day < 6 or >= 22
};
CEngineObject* WorldBase::FindUnit(unsigned id) { return ::WorldFindUnit(this, nullptr, static_cast<int>(id)); }
__declspec(naked) BOOL WorldBase::IsNight() EI_ORIGINAL(0x0055FC10)

__declspec(naked) int __cdecl GameMbscmp(const char*, const char*) EI_ORIGINAL(0x006E848B)

__forceinline float FieldF(const void* object, int offset) {
    return *reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(object) + offset);
}
template <typename T>
__forceinline T Field(const void* object, int offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

} // namespace

__declspec(naked) bool ZoneExit::Contains(float, float) const EI_ORIGINAL(0x004E59F0)

CWorldClient* WorldClient() { return reinterpret_cast<CWorldClient*>(0x007AF5B8); }

// ---- Heights -------------------------------------------------------------------------------------

float CWorldClient::ClampedGroundHeight(float x, float y) {
    // Branch-free in the original (FCOM flags turned into a select mask); NaN picks the first
    // operand of each comparison.
    float cx = !(0.0f >= x) ? x : 0.0f;
    float maxX = StoreFloat(terrain_->width - 1.0f);
    cx = !(maxX >= cx) ? maxX : cx;
    float cy = !(0.0f >= y) ? y : 0.0f;
    float maxY = StoreFloat(terrain_->height - 1.0f);
    cy = !(maxY >= cy) ? maxY : cy;
    return terrain_->HeightAt(cx, cy, 0);
}

float PlatformSet::SurfaceHeight(const Vector3* point, float height) {
    for (PlatformNode* node = head; node; node = node->next) {
        const Platform& p = node->platform;
        float dx = point->x - p.x;
        float dy = point->y - p.y;
        // Every test is "skip when below or unordered" (FCOM + TEST AH,1), so NaN skips.
        if (!(p.radius * p.radius >= dy * dy + dx * dx)) continue;
        float a = point->x - p.origin[0];
        float b = point->y - p.origin[1];
        float c = point->z - p.origin[2];
        // Project onto the platform plane along z, then into its (u, v) frame.
        c = c - (p.normal[1] * b + p.normal[0] * a + p.normal[2] * c) / p.normal[2];
        float u = StoreFloat(p.axisU[1] * b + p.axisU[0] * a + p.axisU[2] * c);
        float v = StoreFloat(p.axisV[1] * b + p.axisV[0] * a + p.axisV[2] * c);
        if (!(u >= -0.5f) || !(v >= -0.5f)) continue;
        if (!(p.sizeU + 0.5f >= u) || !(p.sizeV + 0.5f >= v)) continue;
        float z = StoreFloat(p.axisV[2] * v + p.axisU[2] * u + p.origin[2]);
        height = !(height >= z) ? z : height;
    }
    return height;
}

bool PlatformSet::Contains(float x, float y) {
    for (PlatformNode* node = head; node; node = node->next) {
        const Platform& p = node->platform;
        float dx = x - p.x;
        float dy = y - p.y;
        if (!(p.radius * p.radius >= dx * dx + dy * dy)) continue;
        float a = x - p.origin[0];
        float b = y - p.origin[1];
        float c = -p.origin[2];
        c = c - (p.normal[1] * b + p.normal[2] * c + p.normal[0] * a) / p.normal[2];
        float u = StoreFloat(p.axisU[1] * b + p.axisU[2] * c + p.axisU[0] * a);
        float v = StoreFloat(p.axisV[1] * b + p.axisV[2] * c + p.axisV[0] * a);
        if (!(u >= -0.1f) || !(v >= -0.1f)) continue;
        if (!(p.sizeU + 0.1f >= u) || !(p.sizeV + 0.1f >= v)) continue;
        return true;
    }
    return false;
}

Vector3* MatrixRows::Transform(Vector3* out, const Vector3* v) const {
    float z = m[10] * v->z + m[8] * v->x + m[9] * v->y;
    float y = m[6] * v->z + m[4] * v->x + m[5] * v->y;
    float x = m[2] * v->z + m[1] * v->y + v->x * m[0];
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}

// ---- Units and players ---------------------------------------------------------------------------

CEngineObject* CWorldClient::NearestVisibleUnit(CEngineObject* unit) {
    std::uint8_t* state = Field<std::uint8_t*>(session_, 0x20);
    if (!state || !unit) return nullptr;
    float best = 1e38f;
    CEngineObject* nearest = nullptr;
    for (int i = 0; i < Field<int>(state, 0x50DC); ++i) {
        unsigned id = Field<unsigned*>(state, 0x50D8)[i];
        CEngineObject* other = reinterpret_cast<WorldBase*>(this)->FindUnit(id);
        if (!other || other->GetClassId() != world::kClassUnit) continue;
        if (ei::VCall<int>(other, kSlotUnitIsDead) != 0) continue;
        std::uint32_t side = 1u << (Field<int>(unit, 0x250) & 31);
        if ((Field<std::uint32_t>(other, 0x244) & (side & Field<std::uint32_t>(other, 0x248))) == 0) continue;
        float dy = FieldF(other, 0x20) - FieldF(unit, 0x20);
        float dx = FieldF(other, 0x1C) - FieldF(unit, 0x1C);
        float distance = dx * dx + dy * dy;
        if (!(distance >= best)) {
            best = StoreFloat(distance);
            nearest = other;
        }
    }
    return nearest;
}

void* CWorldClient::ListItem(int index) {
    if (index < listACount_) return listA_[index] + 0x188;
    return listB_[index - listACount_] + 0x188;
}

std::uint8_t* CWorldClient::LocalView() {
    if (session_) return Field<std::uint8_t*>(session_, 0x20);
    return nullptr;
}

bool CWorldClient::IsLocalViewMode2() {
    if (!session_) return false;
    std::uint8_t* state = Field<std::uint8_t*>(session_, 0x20);
    if (!state) return false;
    return Field<int>(state, 0x20) == 2;
}

bool CWorldClient::IsLocalPlayerIdle() {
    if (!session_) return false;
    std::uint8_t* state = Field<std::uint8_t*>(session_, 0x20);
    if (!state) return false;
    auto* controller = Field<CEngineObject*>(Field<std::uint8_t*>(state, 0x1C), 0x1C);
    if (controller && !(static_cast<std::uint32_t>(controller->refCount_) & 0x80000000u) &&
        ei::VCall<bool>(controller, kSlotControllerBusy))
        return false;
    return true;
}

// ---- Exits ---------------------------------------------------------------------------------------

BOOL CWorldClient::FindExitAt(const float* point, int* index) {
    for (int i = 0; i < exitCount_; ++i) {
        if (!exits_[i].Contains(point[0], point[1])) continue;
        if (GameMbscmp(exits_[i].target, "none") == 0) continue;
        if (index) *index = i;
        return 1;
    }
    return 0;
}

float* CWorldClient::ExitCentre(float* out, int index) {
    const ZoneExit& exit = exits_[index];
    float y = (exit.y2 + exit.y1) * 0.5f;
    float x = (exit.x2 + exit.x1) * 0.5f;
    out[0] = x;
    out[1] = y;
    return out;
}

std::uint32_t CWorldClient::EnvironmentFlags(CEngineObject* unit) {
    std::uint32_t flags = 0;
    switch (Field<std::uint32_t>(unit, 0x268)) {
    case 0: flags = 8; break;
    case 1: flags = 4; break;
    case 2: flags = 2; break;
    case 3: flags = 1; break;
    }
    if (GameMbscmp(zoneName_, "gipat") == 0) flags |= 0x10;
    else if (GameMbscmp(zoneName_, "ingos") == 0) flags |= 0x20;
    else if (GameMbscmp(zoneName_, "suslanger") == 0) flags |= 0x40;
    flags |= reinterpret_cast<WorldBase*>(this)->IsNight() ? 0x100 : 0x80;
    flags |= unknown6E0_ == 1 ? 0x400 : 0x200;
    if (unknown7A8_ == 1) return flags | 0x800;
    if (unknown7A8_ == 2) return flags | 0x1000;
    return flags | 0x2000;
}

// ---- Segment (005641F0) --------------------------------------------------------------------------

namespace {

template <typename T> T& Slot(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// Callees, through their addresses (thiscall as fastcall).
__declspec(naked) void __fastcall InputStringEvent(void*, void*, const char*) EI_ORIGINAL(0x005F8070)
__declspec(naked) void __fastcall SoundSetVolumeA(void*, void*, int) EI_ORIGINAL(0x006B7740)
__declspec(naked) void __fastcall SoundSetVolumeB(void*, void*, int) EI_ORIGINAL(0x006B78A0)
__declspec(naked) void __fastcall SoundSetVolumeC(void*, void*, int) EI_ORIGINAL(0x006B7A00)
__declspec(naked) void __fastcall SoundTick(void*) EI_ORIGINAL(0x006B9B10)
__declspec(naked) void __fastcall SessionUpdate(void*) EI_ORIGINAL(0x00665DE0)
__declspec(naked) void __fastcall SessionDisconnect(void*) EI_ORIGINAL(0x00665A10)
__declspec(naked) int __fastcall PendingLookup(void*, void*, const int*, void**) EI_ORIGINAL(0x0056FC60)
__declspec(naked) void __fastcall PendingListRemoveAt(void*, void*, void*) EI_ORIGINAL(0x005704A0)
__declspec(naked) void __fastcall PendingRemoveKey(void*, void*, const int*) EI_ORIGINAL(0x00405350)
__declspec(naked) void __fastcall HudTick(void*, void*, char) EI_ORIGINAL(0x00498480)
__declspec(naked) void __fastcall EffectListErase0(void*, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00572550)
__declspec(naked) void __fastcall EffectListErase1(void*, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00572580)
__declspec(naked) void __fastcall EffectListErase2(void*, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x005725B0)
__declspec(naked) double __fastcall ObjectDistanceTo(void*, void*, const void*) EI_ORIGINAL(0x0068D730)
__declspec(naked) double __fastcall TerrainHeightAt(void*, void*, float, float, int) EI_ORIGINAL(0x006C8680)
__declspec(naked) void __fastcall MapTick(void*) EI_ORIGINAL(0x00694020)

constexpr int kSlotUpdate = 6;            // +0x18
constexpr int kSlotPump = 5;              // +0x14
constexpr int kSlotViewAlive = 23;        // the local view (+0x5C): 0 once the connection is gone
constexpr int kSlotUnits = 14;            // world (+0x38): the units list
constexpr int kSlotAnimate = 25;          // object (+0x64): (game time)
constexpr int kSlotLightUnits = 35;       // world (+0x8C)
constexpr int kSlotMapState = 34;         // world (+0x88)
const char* const kAutosave = reinterpret_cast<const char*>(0x00787704);       // "autosave"
const char* const kDisconnected = reinterpret_cast<const char*>(0x00790084);   // "CWorldClient::Segment()..."
void* const kInputHandler = reinterpret_cast<void*>(0x007C7D50);
void* const kGameFlow = reinterpret_cast<void*>(0x007AFE20);

bool NaN(double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, 8);
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (bits & 0x000FFFFFFFFFFFFFull) != 0;
}
// FCOMP a, b; C0 (a < b, or unordered) selects `ifLess`, else `otherwise` (the original's bit trick).
float SelectLess(float a, float b, float ifLess, float otherwise) {
    return (NaN(a) || NaN(b) || a < b) ? ifLess : otherwise;
}

} // namespace

int CWorldClient::Segment() {
    PlayerIterator unused;   // [ebp-0x68]: never walked, destroyed at the end
    PlayerIterator objectsIt;  // [ebp-0x58]
    void* self = this;
    int frames = ++Slot<int>(self, 0x6EC);
    if (frames == 10) {
        InputStringEvent(kInputHandler, nullptr, kAutosave);
        void* sound = Slot<void*>(self, 0x1CC);
        SoundSetVolumeA(sound, nullptr, *reinterpret_cast<int*>(0x0079BCC8));
        SoundSetVolumeB(Slot<void*>(self, 0x1CC), nullptr, *reinterpret_cast<int*>(0x0079BCCC));
        SoundSetVolumeC(Slot<void*>(self, 0x1CC), nullptr, *reinterpret_cast<int*>(0x0079BCC4));
    } else if (frames == 90 || (frames < 90 && *reinterpret_cast<int*>(0x007AFC9C) == 2)) {
        Slot<int>(Slot<void*>(self, 0x1A8), 0x44) = 0;
    }
    if (void* terrain = Slot<void*>(self, 0x180)) ei::VCall<void>(terrain, kSlotUpdate);
    if (void* p = Slot<void*>(self, 0x1A8)) ei::VCall<void>(p, kSlotUpdate);
    if (void* sound = Slot<void*>(self, 0x1CC)) SoundTick(sound);
    if (!Slot<std::uint8_t>(self, 0x6C)) {
        if (void* p = Slot<void*>(self, 0x1A4)) ei::VCall<void>(p, kSlotUpdate);
    }
    if (void* p = Slot<void*>(self, 0x5C)) ei::VCall<void>(p, kSlotPump);
    if (void* p = Slot<void*>(self, 0x1C8)) ei::VCall<void>(p, kSlotPump);
    if (void* p = Slot<void*>(self, 0x60)) SessionPump(p, nullptr, *reinterpret_cast<void**>(0x0079B5D8));
    if (std::uint8_t* session = session_) {
        void* view = Slot<void*>(session, 0x20);
        if (!Slot<std::uint8_t>(self, 0x6C)) SessionUpdate(session);
        if (ei::VCall<int>(view, kSlotViewAlive) == 0) {
            // The connection is gone. Rebirth's DisableExitOnCrashReport (5 NOPs at 00564330)
            // keeps the session: only the error is reported.
            if (!ei::rebirth::Enabled(ei::rebirth::Patch::DisableExitOnCrashReport)) SessionDisconnect(session_);
            game::FatalError(kDisconnected);
            return 0;
        }
        ei::VCall<void>(view, kSlotUpdate);
    }
    if (!Slot<std::uint8_t>(self, 0x6C)) {
        ++tick_;
        // The objects (list +0xC0) whose update returns 0 leave the pending map (+0xD8) and list.
        for (auto* node = Slot<std::uint8_t*>(self, 0xC0); node;) {
            std::uint8_t* current = node;
            node = *reinterpret_cast<std::uint8_t**>(node);
            CEngineObject* object = Slot<CEngineObject*>(current, 8);
            int done = ei::VCall<int>(object, kSlotUpdate);
            if (done != 0) continue;
            int key = object->id_;
            void* position = nullptr;
            if (PendingLookup(static_cast<std::uint8_t*>(self) + 0xD8, nullptr, &key, &position)) {
                PendingListRemoveAt(static_cast<std::uint8_t*>(self) + 0xBC, nullptr, position);
                PendingRemoveKey(static_cast<std::uint8_t*>(self) + 0xD8, nullptr, &key);
            }
        }
        for (void* object = objectsIt.First(ei::VCall<CEngineObject*>(self, kSlotUnits)); object; object = objectsIt.Next())
            ei::VCall<void>(object, kSlotUpdate);
        if (void* p = Slot<void*>(self, 0x1B0)) ei::VCall<void>(p, kSlotUpdate);
    }
    if (IsNetworkClientOriginal()) {
        std::uintptr_t first = *reinterpret_cast<std::uintptr_t*>(0x007C7D84);
        std::uintptr_t last = *reinterpret_cast<std::uintptr_t*>(0x007C7D88);
        if (static_cast<int>((last - first) & ~3u) > 4) {
            volatile float time = static_cast<float>(tick_) + Slot<float>(self, 0x6F0);
            float now = time;
            for (auto* node = Slot<std::uint8_t*>(self, 0xC0); node;) {
                std::uint8_t* current = node;
                node = *reinterpret_cast<std::uint8_t**>(node);
                ei::VCall<void>(Slot<void*>(current, 8), kSlotAnimate, now);
            }
        }
    }
    if (void* hud = Slot<void*>(self, 0x19C)) HudTick(hud, nullptr, static_cast<char>(Slot<std::uint8_t>(self, 0x6C)));
    if (!Slot<std::uint8_t>(self, 0x6C)) {
        // Three lists of effects (std::list, sentinel head): finished ones (slot 0 returns 0) go.
        using EraseFn = void(__fastcall*)(void*, void*, void**, void*);
        static constexpr EraseFn kErase[3] = {EffectListErase0, EffectListErase1, EffectListErase2};
        for (int l = 0; l < 3; ++l) {
            void* list = static_cast<std::uint8_t*>(self) + 0x1B8 + 4 * l;
            auto* head = Slot<std::uint8_t*>(self, 0x1B8 + 4 * l);
            for (auto* node = *reinterpret_cast<std::uint8_t**>(head); node != head;) {
                std::uint8_t* current = node;
                node = *reinterpret_cast<std::uint8_t**>(node);
                if (ei::VCall<int>(current + 8, 0) == 0) {
                    void* out;
                    kErase[l](list, nullptr, &out, current);
                }
            }
        }
        if (!Slot<std::uint8_t>(self, 0x6C) && *reinterpret_cast<int*>(0x0079BD58) != 0) {
            int& countdown = Slot<int>(self, 0x1C4);
            countdown -= *reinterpret_cast<int*>(0x0078FF34);
            if (countdown <= 0) {
                countdown = *reinterpret_cast<int*>(0x0079BD24);
                // Each light of list +0x1B8 gets a grey level from the farthest unit near it.
                void* leader = *reinterpret_cast<void**>(0x007AF07C);
                for (auto* node = *Slot<std::uint8_t**>(self, 0x1B8); node != Slot<std::uint8_t*>(self, 0x1B8);
                     node = *reinterpret_cast<std::uint8_t**>(node)) {
                    float level = static_cast<float>(ObjectDistanceTo(leader, nullptr, reinterpret_cast<void*>(0x007AFDB0)));
                    PlayerIterator units;
                    for (auto* unit = reinterpret_cast<CEngineObject*>(units.First(ei::VCall<CEngineObject*>(self, kSlotLightUnits)));
                         unit; unit = reinterpret_cast<CEngineObject*>(units.Next())) {
                        if (unit == leader || (unit->flags_ & 0x80)) continue;
                        float nodeX = Slot<float>(node, 0x10), nodeY = Slot<float>(node, 0x14);
                        double dy = static_cast<double>(Slot<float>(unit, 0x20)) - nodeY;
                        double dx = static_cast<double>(Slot<float>(unit, 0x1C)) - nodeX;
                        double squared = dx * dx + dy * dy;
                        double radius = Slot<float>(unit, 0x100);
                        if (!NaN(squared) && !NaN(radius) && squared > radius) continue;
                        float point[3] = {nodeX, nodeY, 0.0f};
                        point[2] = static_cast<float>(TerrainHeightAt(Slot<void*>(self, 0x180), nullptr, nodeX, nodeY, 0));
                        float distance = static_cast<float>(ObjectDistanceTo(unit, nullptr, point));
                        level = SelectLess(level, distance, distance, level);
                    }
                    float clamped = SelectLess(1.0f, level, 1.0f, level);
                    float floor = SelectLess(0.0f, clamped, clamped, 0.0f);
                    volatile float scaled = floor * *reinterpret_cast<const volatile float*>(0x0073C370);
                    std::uint32_t grey = static_cast<std::uint32_t>(x87::fistp(scaled));
                    std::uint32_t colour = ((((grey | 0xFFFFFF00u) << 8) | grey) << 8) | grey;
                    Slot<std::uint32_t>(node, 0x5C) = colour;
                }
            }
        }
    }
    if (tick_ % 32 == 0) MapTick(ei::VCall<void*>(self, kSlotMapState));
    return 1;
}
EI_REPLACE_METHOD(0x005641F0, CWorldClient::Segment);
EI_REBIRTH_IMPLEMENTS(0x005641F0, ei::rebirth::Patch::DisableExitOnCrashReport);

EI_REPLACE_METHOD(0x00563F70, CWorldClient::ClampedGroundHeight);
EI_REPLACE_METHOD(0x00560F00, PlatformSet::SurfaceHeight);
EI_REPLACE_METHOD(0x00561050, PlatformSet::Contains);
EI_REPLACE_METHOD(0x00560E10, MatrixRows::Transform);
EI_REPLACE_METHOD(0x005640A0, CWorldClient::NearestVisibleUnit);
EI_REPLACE_METHOD(0x00564AF0, CWorldClient::ListItem);
EI_REPLACE_METHOD(0x005699F0, CWorldClient::LocalView);
EI_REPLACE_METHOD(0x00566EC0, CWorldClient::IsLocalViewMode2);
EI_REPLACE_METHOD(0x00566EF0, CWorldClient::IsLocalPlayerIdle);
EI_REPLACE_METHOD(0x00567440, CWorldClient::FindExitAt);
EI_REPLACE_METHOD(0x005674C0, CWorldClient::ExitCentre);
EI_REPLACE_METHOD(0x0056D9F0, CWorldClient::EnvironmentFlags);
