// CWorldClient: terrain and platform height queries, unit and exit lookups. See world.h.
#include "world.h"

#include "core_maths.h"
#include "terrain.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

constexpr int kSlotUnitIsDead = 55;     // units: +0xDC, non-zero when dead
constexpr int kSlotControllerBusy = 5;  // player state controller: +0x14

// 0055FB00 (world base): FindObject (slot 40) restricted to units.
struct WorldBase {
    CEngineObject* FindUnit(unsigned id);
    BOOL IsNight();   // 0055FC10: hour of day < 6 or >= 22
};
__declspec(naked) CEngineObject* WorldBase::FindUnit(unsigned) EI_ORIGINAL(0x0055FB00)
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
