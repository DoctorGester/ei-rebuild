// Ground prints: footprints, blood and scorch marks and the client world's functions that add them. A new print
// is built on the stack, copied into its list (sharing the empty geometry arrays) and destroyed; its prints.db
// entry (by the ground's surface class) gives the alpha and the life. See docs/notes/models.md ("Ground prints").
#include "ground_print.h"

#include "camera.h"
#include "core_streams.h"
#include "effects.h"
#include "figure_mesh.h"
#include "game_singletons.h"
#include "ground_decal.h"
#include "model.h"
#include "options.h"
#include "terrain.h"
#include "texture.h"
#include "world.h"
#include "random.h"

#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include "ei/x87.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

constexpr int kTextureKind = static_cast<int>(TextureKind::Effects);                  // the texture manager's kind of the prints' textures
constexpr int kMaxSpreadTicks = 0x2D;
constexpr int kNameCapacity = 0x100;             // Load's name buffer
constexpr float kQuarter = 0.25f;
constexpr float kEighth = 0.125f;
constexpr float kSixteenth = 0.0625f;
constexpr float kAlphaRange = 255.0f;
constexpr float kLift = 0.01f;                   // the drawn quad above the ground
constexpr float kMilli = 0.001f;
constexpr float kAngleMilli = 1000.0f;
constexpr float kSpinDegrees = 30.0f;

constexpr float kHalfPi = /*007C899C*/ 1.5707964f;
constexpr float kTwoPi = /*007C8998*/ 6.2831855f;
// pi and 1 / 180 (doubles; their product is rounded at the FPU's precision, as in the game).
constexpr double kPerDegree = 0.005555555555555556;
bool BloodPrintsOn() { return options::g_options.enableBloodprints != 0; }
bool FootprintsOn() { return options::g_options.enableFootprints != 0; }
bool ScorchMarksOn() { return options::g_options.enableFireprints != 0; }

template <typename T>
void Read(ByteStream* stream, T* value) {
    stream->Read(value, sizeof(T));
}
template <typename T>
void Write(ByteStream* stream, T value) {
    stream->WriteValue(value);
}

int RandomQuarter() { return ClientRandBelow(4); }

// The prints.db entry for the ground at (x, y), or null.
template <typename Record>
const Record* EntryAt(const std::vector<Record>& table, WorldClient* world, float x, float y) {
    auto surface = static_cast<std::uint32_t>(world->terrain->SurfaceClass(x, y));
    return surface < table.size() ? &table[surface] : nullptr;
}

std::uint32_t Channel(float value) { return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(value * kAlphaRange))); }

} // namespace

// ---- GroundPrint --------------------------------------------------------------------------------

GroundPrint::GroundPrint(std::uint8_t newVisible, int newLife, float newAlphaScale, int newLifetime, std::uint32_t newColour) {
    built = false;
    visible = newVisible;
    growth = 0.0f;
    life = static_cast<std::int16_t>(newLife);
    uvAngle = 0.0f;
    angle = 0.0f;
    lit = 0xFF;
    alpha = 1.0f;
    alphaScale = newAlphaScale;
    colour = newColour;
    lifetime = newLifetime;
}

void GroundPrint::SetTexture(const char* name) {
    texture.name.assign(name, name + std::strlen(name));
    CTextureManager* textures = g_clientWorld.textureManager;
    texture.texture.Assign(textures->FindTexture(name));
    if (texture.texture) return;
    texture.texture.Assign(textures->FindOrCreate(kTextureKind, name));
    textures->LoadTexture(texture.texture);
}

GroundPrint::GroundPrint(float x, float y, float newHalfWidth, float newHalfLength, float newAngle, float alphaScale, int life,
                         int lifetime)
    : GroundPrint(1, life, alphaScale, lifetime, 0xFFFFFFFFu) {
    position = {x, y};
    halfWidth = newHalfWidth;
    halfLength = newHalfLength;
    Vector3 point{x, y, 0.0f};
    lit = g_clientWorld.terrain->EdgeFade(point);
    int quarter = RandomQuarter();
    uvScale = {0.5f, 0.5f};
    uvOffset.x = static_cast<float>(quarter & 1) * 0.5f;
    uvOffset.y = static_cast<float>(quarter >> 1) * 0.5f;
    angle = newAngle;
    uvAngle = static_cast<float>(quarter) * kHalfPi;
    SetTexture("fireprints");
    growth = 0.0f;
}

int GroundPrint::Tick() {
    --life;
    halfWidth = halfWidth + growth;
    halfLength = halfLength + growth;
    float t = static_cast<float>(static_cast<double>(lifetime - life) / lifetime);
    EI_NAN_AUDIT(t);
    float spent = t > 0.0f ? t : 0.0f;
    alpha = (1.0f - spent) * alphaScale;
    return life;
}

void GroundPrint::Load(ByteStream* stream) {
    stream->ReadValue(position.x);
    stream->ReadValue(position.y);
    stream->ReadValue(halfWidth);
    stream->ReadValue(halfLength);
    stream->ReadValue(growth);
    stream->ReadValue(life);
    char name[kNameCapacity];
    stream->ReadText(name, kNameCapacity);
    SetTexture(name);
    stream->ReadValue(uvOffset.x);
    stream->ReadValue(uvOffset.y);
    stream->ReadValue(uvScale.x);
    stream->ReadValue(uvScale.y);
    stream->ReadValue(uvAngle);
    stream->ReadValue(angle);
    stream->ReadValue(lit);
    stream->ReadValue(alphaScale);
    stream->ReadValue(lifetime);
    stream->ReadValue(colour);
    built = false;
}

void GroundPrint::Save(ByteStream* stream) {
    stream->WriteValue(position.x);
    stream->WriteValue(position.y);
    stream->WriteValue(halfWidth);
    stream->WriteValue(halfLength);
    stream->WriteValue(growth);
    stream->WriteValue(life);
    stream->WriteString(texture.name.data());
    stream->WriteValue(uvOffset.x);
    stream->WriteValue(uvOffset.y);
    stream->WriteValue(uvScale.x);
    stream->WriteValue(uvScale.y);
    stream->WriteValue(uvAngle);
    stream->WriteValue(angle);
    stream->WriteValue(lit);
    stream->WriteValue(alphaScale);
    stream->WriteValue(lifetime);
    stream->WriteValue(colour);
}

int GroundPrint::Draw() {
    if (!visible) return 1;
    if (!built) {
        // The quad at the height of the ground or the platform under the point.
        WorldClient* world = &g_clientWorld;
        std::vector<VertexPacket> quad;
        Vector3 at{position.x, position.y, 0.0f};
        float z = static_cast<float>(world->platforms.SurfaceHeight(at, world->ClampedGroundHeight(position.x, position.y)));
        quad.resize(1);
        double c = x87::fcos(angle);
        double s = x87::fsin(angle);
        // Corners (+w, -l), (-w, -l), (-w, +l), (+w, +l) turned by the angle.
        const float across[4] = {halfWidth, -halfWidth, -halfWidth, halfWidth};
        const float along[4] = {-halfLength, -halfLength, halfLength, halfLength};
        VertexPacket& corners = quad[0];
        for (int i = 0; i < 4; ++i) {
            corners.block[0][i] = static_cast<float>((across[i] * c + position.x) - along[i] * s);
            corners.block[1][i] = static_cast<float>((across[i] * s + along[i] * c) + position.y);
            corners.block[2][i] = z;
            corners.block[3][i] = 1.0f;
        }
        Vector3 down{0.0f, 0.0f, -1.0f};
        Vector3 above{position.x, position.y, z + 1.0f};
        world->ProjectDecal(down, above, 0.0f, corners, &positions, &texCoords, &indices);

        // The texture coordinates turned about the tile's centre, then into the tile. The loop is
        // unrolled four times: the fourth vertex stores its u part to a float before scaling
        // it, the others their v part.
        float uc = static_cast<float>(x87::fcos(uvAngle));
        float us = static_cast<float>(x87::fsin(uvAngle));
        int vertices = std::ssize(positions) * 4;
        for (int i = 0; i < vertices; ++i) {
            DecalTexCoord& uv = texCoords[i];
            double du = uv.u - 0.5f;
            double dv = uv.v - 0.5f;
            double u = (du * uc - dv * us) + 0.5f;
            double v = (dv * uc + du * us) + 0.5f;
            if ((i & 3) == 3) u = static_cast<float>(u);
            else v = static_cast<float>(v);
            uv.u = static_cast<float>(u * uvScale.x + uvOffset.x);
            uv.v = static_cast<float>(v * uvScale.y + uvOffset.y);
        }
        for (int i = 0; i < std::ssize(positions); ++i)
            for (float& height : positions[i].block[2]) height = height + kLift;
        built = true;
    }
    std::uint32_t fadeAlpha = static_cast<std::uint32_t>(x87::fistp(static_cast<float>(alpha * kAlphaRange)));
    colour = (colour & 0xFFFFFFu) | fadeAlpha << 24;
    TextureDevice* device = g_sceneRenderer.device;
    device->BindNamedTexture(0, &texture);
    return device->DrawFlatBlocks(positions.data(), texCoords.data(), std::ssize(positions),
                                  indices.data(), std::ssize(indices), colour, static_cast<std::uint32_t>(lit) << 24);
}

// ---- Footprint ----------------------------------------------------------------------------------

Footprint::Footprint(float x, float y, float newHalfWidth, float newHalfLength, float newAngle, float alphaScale, int life,
                     int lifetime, std::uint32_t tile, float newScale, int visible, int newKind)
    : GroundPrint(static_cast<std::uint8_t>(visible), life, alphaScale, lifetime, 0xFFFFFFFFu) {
    kind = newKind;
    scale = newScale;
    position = {x, y};
    halfWidth = newHalfWidth;
    halfLength = newHalfLength;
    Vector3 point{x, y, 0.0f};
    lit = g_clientWorld.terrain->EdgeFade(point);
    // The tile's parts are below 2^16: converted as ints.
    int index = static_cast<int>(tile & 0xFFFF);
    uvOffset.x = static_cast<float>(index & 7) * kEighth + static_cast<float>(static_cast<int>(tile >> 16)) * kSixteenth;
    angle = newAngle;
    uvScale = {kSixteenth, kEighth};
    uvAngle = 0.0f;
    uvOffset.y = static_cast<float>(index >> 3) * kEighth;
    if (kind == -1) {
        SetTexture("footprints");
    } else {
        char name[64];
        std::snprintf(name, sizeof(name), "footprints%d", kind + 1);
        SetTexture(name);
    }
    growth = 0.0f;
}

void Footprint::Load(ByteStream* stream) {
    GroundPrint::Load(stream);
    stream->ReadValue(scale);
    stream->ReadValue(kind);
}

void Footprint::Save(ByteStream* stream) {
    GroundPrint::Save(stream);
    stream->WriteValue(scale);
    stream->WriteValue(kind);
}

// ---- BloodPrint ---------------------------------------------------------------------------------

BloodPrint::BloodPrint(float x, float y, float halfSize, float fullSize, float alphaScale, int life, int lifetime,
                       std::uint32_t tile, std::uint32_t colour, std::uint8_t newFresh)
    : GroundPrint(1, life, alphaScale, lifetime, colour) {
    position = {x, y};
    halfWidth = halfSize;
    halfLength = halfSize;
    Vector3 point{x, y, 0.0f};
    lit = g_clientWorld.terrain->EdgeFade(point);
    int index = static_cast<int>(tile & 0xFFFF);
    int high = static_cast<int>(tile >> 16);
    uvOffset.x = static_cast<float>(index & 1) * kQuarter + static_cast<float>(high & 1) * 0.5f;
    uvScale = {kQuarter, kQuarter};
    uvOffset.y = static_cast<float>(index >> 1) * kQuarter + static_cast<float>(high >> 1) * 0.5f;
    uvAngle = static_cast<float>(RandomQuarter()) * kHalfPi;
    int steps = x87::fistp(static_cast<float>(kTwoPi * kAngleMilli));
    angle = static_cast<float>(ClientRandBelow(steps)) * kMilli;
    SetTexture("bloodprints");
    int spread = life / 2;
    if (spread > kMaxSpreadTicks) spread = kMaxSpreadTicks;
    spreadTicks = static_cast<std::int16_t>(spread);
    kind = high;
    fresh = newFresh;
    float degrees = static_cast<float>(kSpinDegrees / static_cast<double>(life));
    float radiansPerDegree = static_cast<float>(kPiDouble * kPerDegree);
    spin = degrees * radiansPerDegree;
    growth = static_cast<float>((fullSize - halfWidth) / static_cast<double>(spreadTicks));
}

int BloodPrint::Tick() {
    if (--spreadTicks < 0) growth = 0.0f;
    else built = false;
    angle = angle + spin;
    return GroundPrint::Tick();
}

void BloodPrint::Load(ByteStream* stream) {
    GroundPrint::Load(stream);
    stream->ReadValue(spin);
    stream->ReadValue(spreadTicks);
    stream->ReadValue(kind);
    stream->ReadValue(fresh);
}

void BloodPrint::Save(ByteStream* stream) {
    GroundPrint::Save(stream);
    stream->WriteValue(spin);
    stream->WriteValue(spreadTicks);
    stream->WriteValue(kind);
    stream->WriteValue(fresh);
}

// ---- The client world's prints ------------------------------------------------------------------

void WorldClient::AddBloodPrint(float x, float y, float halfSize, float fullSize, std::uint32_t tile, int fresh) {
    if (!BloodPrintsOn()) return;
    const PrintRecord* entry = EntryAt(g_printsDatabase.blood, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, rainSnow.mode != 0);
    EI_NAN_AUDIT(look.alpha);
    if (look.alpha <= 0.0f || look.life <= 0 || look.lifetime <= 0) return;
    std::uint32_t colour = ((Channel(frameSun.r) | 0x100) << 8 | Channel(frameSun.g)) << 8 | Channel(frameSun.b);
    bloodPrints.emplace_back(x, y, halfSize, fullSize, look.alpha, look.life, look.lifetime, tile, colour,
                              static_cast<std::uint8_t>(fresh));
}

int WorldClient::AddFootprint(float x, float y, float halfWidth, float halfLength, float angle, std::uint32_t tile,
                               float scale, int visible, int bloodKind) {
    if (!FootprintsOn()) return -1;
    // Stepping into a blood print (not a fresh hit's) makes bloody footprints of its kind.
    bool inBlood = false;
    float reach = (halfWidth + halfLength) * 0.5f;
    for (const BloodPrint& blood : bloodPrints) {
        float d2 = DistanceSquared2D(blood.position, Vector2{x, y});
        float limit = (blood.halfWidth + reach) * (blood.halfWidth + reach);
        if (limit >= d2 && !blood.fresh) {
            bloodKind = blood.kind;
            inBlood = true;
            break;
        }
    }
    const PrintsDatabase& prints = g_printsDatabase;
    const PrintLook* look = nullptr;
    if (bloodKind == -1) {
        if (const PrintRecord2* entry = EntryAt(prints.footprints, this, x, y)) look = &LookOf(*entry, rainSnow.mode != 0);
    } else {
        if (const PrintRecord* entry = EntryAt(prints.blood, this, x, y)) look = &LookOf(*entry, rainSnow.mode != 0);
    }
    if (look && look->alpha > 0.0f && look->life > 0 && look->lifetime > 0) {
        footprints.emplace_back(x, y, halfWidth, halfLength, angle, look->alpha, look->life, look->lifetime, tile, scale,
                                 visible, bloodKind);
    }
    return inBlood ? bloodKind : -1;
}

void WorldClient::AddScorchMark(float x, float y, float halfWidth, float halfLength, float angle, float alpha) {
    if (!ScorchMarksOn()) return;
    const PrintRecord* entry = EntryAt(g_printsDatabase.scorch, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, rainSnow.mode != 0);
    EI_NAN_AUDIT(alpha);
    if (alpha <= 0.0f || look.life <= 0 || look.lifetime <= 0) return;
    scorchMarks.emplace_back(x, y, halfWidth, halfLength, angle, alpha, look.life, look.lifetime);
}
