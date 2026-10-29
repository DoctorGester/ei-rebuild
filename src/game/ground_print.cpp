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

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include "ei/x87.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

constexpr int kTextureKind = 6;                  // the texture manager's kind of the prints' textures
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
bool BloodPrintsOn() { return options::g_options->enableBloodprints_ != 0; }
bool FootprintsOn() { return options::g_options->enableFootprints_ != 0; }
bool ScorchMarksOn() { return options::g_options->enableFireprints_ != 0; }

template <typename T>
void Read(CStream* stream, T* value) {
    stream->Read(value, sizeof(T));
}
template <typename T>
void Write(CStream* stream, T value) {
    stream->WriteValue(value);
}

int RandomQuarter() { return libcrt::RandBelow(4); }

// The prints.db entry for the ground at (x, y), or null.
template <typename Record>
const Record* EntryAt(const std::vector<Record>& table, CWorldClient* world, float x, float y) {
    auto surface = static_cast<std::uint32_t>(world->terrain_->SurfaceClass(x, y));
    return surface < table.size() ? &table[surface] : nullptr;
}

std::uint32_t Channel(float value) { return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(value * kAlphaRange))); }

} // namespace

// ---- GroundPrint --------------------------------------------------------------------------------

GroundPrint::GroundPrint(std::uint8_t visible, int life, float alphaScale, int lifetime, std::uint32_t colour) {
    built_ = false;
    visible_ = visible;
    growth_ = 0.0f;
    life_ = static_cast<std::int16_t>(life);
    uvAngle_ = 0.0f;
    angle_ = 0.0f;
    lit_ = 0xFF;
    alpha_ = 1.0f;
    alphaScale_ = alphaScale;
    colour_ = colour;
    lifetime_ = lifetime;
}

void GroundPrint::SetTexture(const char* name) {
    texture_.name.assign(name, name + std::strlen(name));
    CTextureManager* textures = g_clientWorld->textureManager_;
    texture_.texture.Assign(textures->FindTexture(name));
    if (texture_.texture) return;
    texture_.texture.Assign(textures->FindOrCreate(kTextureKind, name));
    textures->LoadTexture(texture_.texture);
}

GroundPrint::GroundPrint(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
                         int lifetime)
    : GroundPrint(1, life, alphaScale, lifetime, 0xFFFFFFFFu) {
    position_ = {x, y};
    halfWidth_ = halfWidth;
    halfLength_ = halfLength;
    Vector3 point{x, y, 0.0f};
    lit_ = g_clientWorld->terrain_->EdgeFade(&point);
    int quarter = RandomQuarter();
    uvScale_ = {0.5f, 0.5f};
    uvOffset_.x = static_cast<float>(quarter & 1) * 0.5f;
    uvOffset_.y = static_cast<float>(quarter >> 1) * 0.5f;
    angle_ = angle;
    uvAngle_ = static_cast<float>(quarter) * kHalfPi;
    SetTexture("fireprints");
    growth_ = 0.0f;
}

int GroundPrint::Tick() {
    --life_;
    halfWidth_ = halfWidth_ + growth_;
    halfLength_ = halfLength_ + growth_;
    float t = static_cast<float>(static_cast<double>(lifetime_ - life_) / lifetime_);
    EI_NAN_AUDIT(t);
    float spent = t > 0.0f ? t : 0.0f;
    alpha_ = (1.0f - spent) * alphaScale_;
    return life_;
}

void GroundPrint::Load(CStream* stream) {
    stream->ReadValue(position_.x);
    stream->ReadValue(position_.y);
    stream->ReadValue(halfWidth_);
    stream->ReadValue(halfLength_);
    stream->ReadValue(growth_);
    stream->ReadValue(life_);
    char name[kNameCapacity];
    stream->ReadText(name, kNameCapacity);
    SetTexture(name);
    stream->ReadValue(uvOffset_.x);
    stream->ReadValue(uvOffset_.y);
    stream->ReadValue(uvScale_.x);
    stream->ReadValue(uvScale_.y);
    stream->ReadValue(uvAngle_);
    stream->ReadValue(angle_);
    stream->ReadValue(lit_);
    stream->ReadValue(alphaScale_);
    stream->ReadValue(lifetime_);
    stream->ReadValue(colour_);
    built_ = false;
}

void GroundPrint::Save(CStream* stream) {
    stream->WriteValue(position_.x);
    stream->WriteValue(position_.y);
    stream->WriteValue(halfWidth_);
    stream->WriteValue(halfLength_);
    stream->WriteValue(growth_);
    stream->WriteValue(life_);
    stream->WriteString(texture_.name.data());
    stream->WriteValue(uvOffset_.x);
    stream->WriteValue(uvOffset_.y);
    stream->WriteValue(uvScale_.x);
    stream->WriteValue(uvScale_.y);
    stream->WriteValue(uvAngle_);
    stream->WriteValue(angle_);
    stream->WriteValue(lit_);
    stream->WriteValue(alphaScale_);
    stream->WriteValue(lifetime_);
    stream->WriteValue(colour_);
}

int GroundPrint::Draw() {
    if (!visible_) return 1;
    if (!built_) {
        // The quad at the height of the ground or the platform under the point.
        CWorldClient* world = &g_clientWorld.Get();
        std::vector<VertexPacket> quad;
        Vector3 at{position_.x, position_.y, 0.0f};
        float z = static_cast<float>(world->platforms_.SurfaceHeight(&at, world->ClampedGroundHeight(position_.x, position_.y)));
        quad.resize(1);
        double c = x87::fcos(angle_);
        double s = x87::fsin(angle_);
        // Corners (+w, -l), (-w, -l), (-w, +l), (+w, +l) turned by the angle.
        const float across[4] = {halfWidth_, -halfWidth_, -halfWidth_, halfWidth_};
        const float along[4] = {-halfLength_, -halfLength_, halfLength_, halfLength_};
        VertexPacket& corners = quad[0];
        for (int i = 0; i < 4; ++i) {
            corners.block[0][i] = static_cast<float>((across[i] * c + position_.x) - along[i] * s);
            corners.block[1][i] = static_cast<float>((across[i] * s + along[i] * c) + position_.y);
            corners.block[2][i] = z;
            corners.block[3][i] = 1.0f;
        }
        Vector3 down{0.0f, 0.0f, -1.0f};
        Vector3 above{position_.x, position_.y, z + 1.0f};
        VertexPacket* quadCorners = quad.data();
        world->ProjectDecal(&down, &above, 0.0f, &quadCorners, &positions_, &texCoords_, &indices_, static_cast<unsigned char>(1), static_cast<unsigned char>(1));

        // The texture coordinates turned about the tile's centre, then into the tile. The loop is
        // unrolled four times: the fourth vertex stores its u part to a float before scaling
        // it, the others their v part.
        float uc = static_cast<float>(x87::fcos(uvAngle_));
        float us = static_cast<float>(x87::fsin(uvAngle_));
        int vertices = std::ssize(positions_) * 4;
        for (int i = 0; i < vertices; ++i) {
            DecalTexCoord& uv = texCoords_[i];
            double du = uv.u - 0.5f;
            double dv = uv.v - 0.5f;
            double u = (du * uc - dv * us) + 0.5f;
            double v = (dv * uc + du * us) + 0.5f;
            if ((i & 3) == 3) u = static_cast<float>(u);
            else v = static_cast<float>(v);
            uv.u = static_cast<float>(u * uvScale_.x + uvOffset_.x);
            uv.v = static_cast<float>(v * uvScale_.y + uvOffset_.y);
        }
        for (int i = 0; i < std::ssize(positions_); ++i)
            for (float& height : positions_[i].block[2]) height = height + kLift;
        built_ = true;
    }
    std::uint32_t alpha = static_cast<std::uint32_t>(x87::fistp(static_cast<float>(alpha_ * kAlphaRange)));
    colour_ = (colour_ & 0xFFFFFFu) | alpha << 24;
    CTextureDevice* device = g_sceneRenderer->device_;
    device->BindNamedTexture(0, &texture_);
    return device->DrawFlatBlocks(positions_.data(), texCoords_.data(), std::ssize(positions_),
                                  indices_.data(), std::ssize(indices_), colour_, static_cast<std::uint32_t>(lit_) << 24, nullptr, 1.0f);
}

// ---- Footprint ----------------------------------------------------------------------------------

Footprint::Footprint(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
                     int lifetime, std::uint32_t tile, float scale, int visible, int kind)
    : GroundPrint(static_cast<std::uint8_t>(visible), life, alphaScale, lifetime, 0xFFFFFFFFu) {
    kind_ = kind;
    scale_ = scale;
    position_ = {x, y};
    halfWidth_ = halfWidth;
    halfLength_ = halfLength;
    Vector3 point{x, y, 0.0f};
    lit_ = g_clientWorld->terrain_->EdgeFade(&point);
    // The tile's parts are below 2^16: converted as ints.
    int index = static_cast<int>(tile & 0xFFFF);
    uvOffset_.x = static_cast<float>(index & 7) * kEighth + static_cast<float>(static_cast<int>(tile >> 16)) * kSixteenth;
    angle_ = angle;
    uvScale_ = {kSixteenth, kEighth};
    uvAngle_ = 0.0f;
    uvOffset_.y = static_cast<float>(index >> 3) * kEighth;
    if (kind_ == -1) {
        SetTexture("footprints");
    } else {
        char name[64];
        std::snprintf(name, sizeof(name), "footprints%d", kind_ + 1);
        SetTexture(name);
    }
    growth_ = 0.0f;
}

void Footprint::Load(CStream* stream) {
    GroundPrint::Load(stream);
    stream->ReadValue(scale_);
    stream->ReadValue(kind_);
}

void Footprint::Save(CStream* stream) {
    GroundPrint::Save(stream);
    stream->WriteValue(scale_);
    stream->WriteValue(kind_);
}

// ---- BloodPrint ---------------------------------------------------------------------------------

BloodPrint::BloodPrint(float x, float y, float halfSize, float fullSize, float alphaScale, int life, int lifetime,
                       std::uint32_t tile, std::uint32_t colour, std::uint8_t fresh)
    : GroundPrint(1, life, alphaScale, lifetime, colour) {
    position_ = {x, y};
    halfWidth_ = halfSize;
    halfLength_ = halfSize;
    Vector3 point{x, y, 0.0f};
    lit_ = g_clientWorld->terrain_->EdgeFade(&point);
    int index = static_cast<int>(tile & 0xFFFF);
    int high = static_cast<int>(tile >> 16);
    uvOffset_.x = static_cast<float>(index & 1) * kQuarter + static_cast<float>(high & 1) * 0.5f;
    uvScale_ = {kQuarter, kQuarter};
    uvOffset_.y = static_cast<float>(index >> 1) * kQuarter + static_cast<float>(high >> 1) * 0.5f;
    uvAngle_ = static_cast<float>(RandomQuarter()) * kHalfPi;
    int steps = x87::fistp(static_cast<float>(kTwoPi * kAngleMilli));
    angle_ = static_cast<float>(libcrt::RandBelow(steps)) * kMilli;
    SetTexture("bloodprints");
    int spread = life / 2;
    if (spread > kMaxSpreadTicks) spread = kMaxSpreadTicks;
    spreadTicks_ = static_cast<std::int16_t>(spread);
    kind_ = high;
    fresh_ = fresh;
    float degrees = static_cast<float>(kSpinDegrees / static_cast<double>(life));
    float radiansPerDegree = static_cast<float>(kPiDouble * kPerDegree);
    spin_ = degrees * radiansPerDegree;
    growth_ = static_cast<float>((fullSize - halfWidth_) / static_cast<double>(spreadTicks_));
}

int BloodPrint::Tick() {
    if (--spreadTicks_ < 0) growth_ = 0.0f;
    else built_ = false;
    angle_ = angle_ + spin_;
    return GroundPrint::Tick();
}

void BloodPrint::Load(CStream* stream) {
    GroundPrint::Load(stream);
    stream->ReadValue(spin_);
    stream->ReadValue(spreadTicks_);
    stream->ReadValue(kind_);
    stream->ReadValue(fresh_);
}

void BloodPrint::Save(CStream* stream) {
    GroundPrint::Save(stream);
    stream->WriteValue(spin_);
    stream->WriteValue(spreadTicks_);
    stream->WriteValue(kind_);
    stream->WriteValue(fresh_);
}

// ---- The client world's prints ------------------------------------------------------------------

void CWorldClient::AddBloodPrint(float x, float y, float halfSize, float fullSize, std::uint32_t tile, int fresh) {
    if (!BloodPrintsOn()) return;
    const PrintRecord* entry = EntryAt(g_printsDatabase.blood, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, rainSnow_.mode != 0);
    EI_NAN_AUDIT(look.alpha);
    if (look.alpha <= 0.0f || look.life <= 0 || look.lifetime <= 0) return;
    std::uint32_t colour = ((Channel(frameSun_.r) | 0x100) << 8 | Channel(frameSun_.g)) << 8 | Channel(frameSun_.b);
    bloodPrints_.emplace_back(x, y, halfSize, fullSize, look.alpha, look.life, look.lifetime, tile, colour,
                              static_cast<std::uint8_t>(fresh));
}

int CWorldClient::AddFootprint(float x, float y, float halfWidth, float halfLength, float angle, std::uint32_t tile,
                               float scale, int visible, int bloodKind) {
    if (!FootprintsOn()) return -1;
    // Stepping into a blood print (not a fresh hit's) makes bloody footprints of its kind.
    bool inBlood = false;
    float reach = (halfWidth + halfLength) * 0.5f;
    for (const BloodPrint& blood : bloodPrints_) {
        float d2 = DistanceSquared2D(blood.position_, Vector2{x, y});
        float limit = (blood.halfWidth_ + reach) * (blood.halfWidth_ + reach);
        if (limit >= d2 && !blood.fresh_) {
            bloodKind = blood.kind_;
            inBlood = true;
            break;
        }
    }
    const PrintsDatabase& prints = g_printsDatabase;
    const PrintLook* look = nullptr;
    if (bloodKind == -1) {
        if (const PrintRecord2* entry = EntryAt(prints.footprints, this, x, y)) look = &LookOf(*entry, rainSnow_.mode != 0);
    } else {
        if (const PrintRecord* entry = EntryAt(prints.blood, this, x, y)) look = &LookOf(*entry, rainSnow_.mode != 0);
    }
    if (look && look->alpha > 0.0f && look->life > 0 && look->lifetime > 0) {
        footprints_.emplace_back(x, y, halfWidth, halfLength, angle, look->alpha, look->life, look->lifetime, tile, scale,
                                 visible, bloodKind);
    }
    return inBlood ? bloodKind : -1;
}

void CWorldClient::AddScorchMark(float x, float y, float halfWidth, float halfLength, float angle, float alpha) {
    if (!ScorchMarksOn()) return;
    const PrintRecord* entry = EntryAt(g_printsDatabase.scorch, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, rainSnow_.mode != 0);
    EI_NAN_AUDIT(alpha);
    if (alpha <= 0.0f || look.life <= 0 || look.lifetime <= 0) return;
    scorchMarks_.emplace_back(x, y, halfWidth, halfLength, angle, alpha, look.life, look.lifetime);
}
