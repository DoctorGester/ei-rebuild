#include "ai_map.h"

#include "ei/hook.h"
#include "ei/x87.h"

bool CAIMap::CanStandAt(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    if (unit->GetClassId() == kClassUnit) {
        int level = x87::fistp((2.0f - unit->standingHeight) * 16.0f);
        if (level < 0) level = 0;
        *reason = 1;
        if (cells[y][x].level > static_cast<std::uint8_t>(level)) return false;
        *reason = 2;
        if (waterDepth[y][x] > (level & 0xFF)) return false;
    }
    *reason = 0;
    return true;
}
EI_REPLACE_METHOD(0x005B80F0, CAIMap::CanStandAt);

bool CAIMap::IsPassable(CUnit* unit, int x, int y) {
    if (unit->GetClassId() != kClassUnit) return true;
    int reason;
    if (!CanStandAt(unit, x, y, &reason)) return false;
    return ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
}
EI_REPLACE_METHOD(0x005B79D0, CAIMap::IsPassable);

namespace {
// Transparency of an occluder class (0073 global at 00791744).
const float* Transparency() { return reinterpret_cast<const float*>(0x00791744); }
constexpr float kMinVisibility = 0.0001f;

// An occluder record in a sector's list (sector + 0x44), for cells holding more than one.
#pragma pack(push, 1)
struct Occluder {
    std::int16_t bottom;    // the ray height must be <= bottom ...
    std::int16_t top;       // ... and >= top for the occluder to count
    std::uint8_t unknown_4;
    std::int8_t transparency;
    std::int16_t next;      // index of the next record, < 0 ends the chain
};
#pragma pack(pop)
} // namespace

float CAIMap::LineOfSight(const Vector3* from, const Vector3* to) {
    float dx = to->x - from->x, dy = to->y - from->y;
    float dz = to->z - from->z;
    float length = static_cast<float>(x87::fsqrt(dz * dz + dy * dy + dx * dx) / (1.0f * 0.5f));   // half-cells
    if (length == 0.0f || length != length) return 1.0f;   // FCOM sets "equal" for NaN too
    float inverse = 1.0f / length;
    int steps = x87::fistp(length);

    // 16.16 fixed point position and step, in half-cells (x, y) and AI height units (z).
    int x = x87::fistp(from->x * 65536.0f / (1.0f * 0.5f));
    int y = x87::fistp(from->y * 65536.0f / (1.0f * 0.5f));
    int z = x87::fistp(from->z * heightScale) << 16;
    int stepX = x87::fistp(dx * inverse * 65536.0f / (1.0f * 0.5f));
    int stepY = x87::fistp(dy * inverse * 65536.0f / (1.0f * 0.5f));
    int toZ = x87::fistp(to->z * heightScale);
    int stepZ = x87::fistp(static_cast<float>(static_cast<double>((toZ - (z >> 16)) << 16) * inverse));

    auto outside = [this](int fx, int fy) {
        int cx = fx >> 16, cy = fy >> 16;
        return cx < 0 || cy < 0 || cx >= width || cy >= height;
    };
    if (outside(x, y) || outside(stepX * steps + x, stepY * steps + y)) return 0.0f;

    float visibility = 1.0f;
    x += stepX;
    y += stepY;
    z += stepZ;
    for (int i = 0; i < steps; ++i, x += stepX, y += stepY, z += stepZ) {
        int cx = x >> 16, cy = y >> 16, cz = z >> 16;
        if (cells[cy][cx].groundLevel >= cz) return 0.0f;

        std::uint32_t word = occluders[cy][cx];
        if (word & 0x100) continue;
        if (!(word & 0x80)) {
            // A single occluder packed into the word.
            int bottom = (word >> 12) & 0x3FF, top = static_cast<int>(word >> 22);
            if (bottom < cz || top > cz) continue;
            visibility *= Transparency()[(word >> 5) & 3];
            if (!(visibility >= kMinVisibility)) return kMinVisibility;
            continue;
        }
        // Several occluders: a chain in the sector's record list.
        int sector = (cy / 32) * sectorsPerRow + cx / 32;
        auto* records = *reinterpret_cast<Occluder**>(sectors + sector * 0x54 + 0x44);
        for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
            const Occluder& o = records[index];
            if (o.bottom < cz || o.top > cz) continue;
            visibility *= Transparency()[o.transparency];
            if (!(visibility >= kMinVisibility)) return kMinVisibility;
        }
    }
    return visibility;
}
EI_REPLACE_METHOD(0x005BC3E0, CAIMap::LineOfSight);
