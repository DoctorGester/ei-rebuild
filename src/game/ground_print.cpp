// Ground prints: footprints, blood and scorch marks (006CECD0-006CFEA0) and the client world's
// functions that add them (0056CE90, 0056D150, 0056D4C0). See ground_print.h and
// docs/notes/models.md ("Ground prints").
//
// A new print is built on the stack, copied into its list (the list instantiations' own insert:
// node allocation and the records' compiler-generated copy constructor, which shares the empty
// geometry arrays) and destroyed. Its table entry in prints.db (by the ground's surface class)
// gives the alpha and the life; the client world's +0x7A8 picks the entry's second set.
#include "ground_print.h"

#include "camera.h"              // TheTerrain
#include "core_streams.h"
#include "effects.h"             // CRainSnow
#include "figure_mesh.h"         // VertexPacket
#include "ground_decal.h"        // DecalTexCoord
#include "model.h"               // ThePrintsDatabase
#include "originals_effects.h"
#include "originals_render.h"    // DeviceDrawFlatOriginal
#include "originals_ui.h"        // TextureNamedOriginal, TextureLoadOriginal
#include "originals_world.h"     // ClientProjectDecalOriginal
#include "terrain.h"             // Terrain::SurfaceClass
#include "texture.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kTextureKind = 6;                  // the texture manager's kind of the prints' textures
constexpr int kMaxSpreadTicks = 0x2D;
constexpr int kNameCapacity = 0x100;             // Load's name buffer
constexpr float kHalf = 0.5f;                    // 0073B86C
constexpr float kQuarter = 0.25f;                // 0073C3B0
constexpr float kEighth = 0.125f;                // 0073C520
constexpr float kSixteenth = 0.0625f;            // 0073D040
constexpr float kOne = 1.0f;                     // 0073B84C
constexpr float kAlphaRange = 255.0f;            // 0073C370
constexpr float kLift = 0.01f;                   // 0073C548, the drawn quad above the ground
constexpr float kMilli = 0.001f;                 // 0073CCF4
constexpr float kAngleMilli = 1000.0f;           // 0073C34C
constexpr float kSpinDegrees = 30.0f;            // 0073BAF0

// Globals.
CWorldClient* ClientWorld() { return reinterpret_cast<CWorldClient*>(0x007AF5B8); }
CTextureManager* Textures() { return *reinterpret_cast<CTextureManager* const*>(0x007AF744); }
// pi / 2 and 2 pi as floats (set by the static initialisers 006CEC90 / 006CEC80).
float HalfPi() { return *reinterpret_cast<const float*>(0x007C899C); }
float TwoPi() { return *reinterpret_cast<const float*>(0x007C8998); }
// pi and 1 / 180 (doubles; their product is rounded at the FPU's precision, as in the original).
double Pi() { return *reinterpret_cast<const double*>(0x00744238); }
double PerDegree() { return *reinterpret_cast<const double*>(0x0073BA30); }
// The prints settings: blood, footprints, scorch marks (0 off).
bool BloodPrintsOn() { return *reinterpret_cast<const int*>(0x0079BD50) != 0; }
bool FootprintsOn() { return *reinterpret_cast<const int*>(0x0079BD4C) != 0; }
bool ScorchMarksOn() { return *reinterpret_cast<const int*>(0x0079BD54) != 0; }

// The lists' insert(position, value) (thiscall, RET 0xC; `out` receives the new node).
__declspec(naked) void __fastcall BloodPrintsInsert(stlp::list<BloodPrint>*, void*, stlp::list<BloodPrint>::Node** /*out*/,
                                                    stlp::list<BloodPrint>::Node* /*position*/, const BloodPrint*)
    EI_ORIGINAL(0x0056ED90)
__declspec(naked) void __fastcall FootprintsInsert(stlp::list<Footprint>*, void*, stlp::list<Footprint>::Node** /*out*/,
                                                   stlp::list<Footprint>::Node* /*position*/, const Footprint*)
    EI_ORIGINAL(0x0056EED0)
__declspec(naked) void __fastcall ScorchMarksInsert(stlp::list<GroundPrint>*, void*,
                                                    stlp::list<GroundPrint>::Node** /*out*/,
                                                    stlp::list<GroundPrint>::Node* /*position*/, const GroundPrint*)
    EI_ORIGINAL(0x0056F010)

// 004EBE60, inlined: *slot = value with counted references, the old one released.
void AssignCounted(CTexture** slot, CTexture* value) {
    CTexture* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old) old->Release();
}

template <typename T>
void Read(CStream* stream, T* value) {
    stream->Read(value, sizeof(T));
}
template <typename T>
void Write(CStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

// (rand() * 4 / 0x7FFF) % 4: a quarter turn.
int RandomQuarter() { return gamecrt::rand() * 4 / 0x7FFF % 4; }

// The prints.db entry for the ground at (x, y), or null.
template <typename Record>
const Record* EntryAt(const stlp::vector<Record>& table, CWorldClient* world, float x, float y) {
    auto surface = static_cast<std::uint32_t>(world->terrain_->SurfaceClass(x, y));
    return surface < table.size() ? &table[surface] : nullptr;
}

// The colour's channel: FISTP of the float channel * 255.
std::uint32_t Channel(float value) { return static_cast<std::uint32_t>(x87::fistp(static_cast<float>(value * kAlphaRange))); }

} // namespace

// ---- GroundPrint --------------------------------------------------------------------------------

void GroundPrint::ConstructBase(std::uint8_t visible, int life, float alphaScale, int lifetime, std::uint32_t colour) {
    built_ = false;
    visible_ = visible;
    growth_ = 0.0f;
    life_ = static_cast<std::int16_t>(life);
    stlp::string::Reserve(texture_.name, 8);
    texture_.texture = nullptr;
    uvAngle_ = 0.0f;
    angle_ = 0.0f;
    lit_ = 0xFF;
    alpha_ = 1.0f;
    alphaScale_ = alphaScale;
    colour_ = colour;
    lifetime_ = lifetime;
    positions_.Construct();
    texCoords_.Construct();
    indices_.Construct();
}

void GroundPrint::SetTexture(const char* name) {
    texture_.name.Assign(name, name + std::strlen(name));
    CTextureManager* textures = Textures();
    AssignCounted(&texture_.texture, textures->FindTexture(name));
    if (texture_.texture) return;
    AssignCounted(&texture_.texture, static_cast<CTexture*>(TextureNamedOriginal(textures, nullptr, kTextureKind, name)));
    TextureLoadOriginal(textures, nullptr, texture_.texture);
}

GroundPrint* GroundPrint::CopyConstruct(const GroundPrint* other) {
    built_ = other->built_;
    visible_ = other->visible_;
    position_ = other->position_;
    halfWidth_ = other->halfWidth_;
    halfLength_ = other->halfLength_;
    growth_ = other->growth_;
    life_ = other->life_;
    texture_.Copy(&other->texture_);
    uvOffset_ = other->uvOffset_;
    uvScale_ = other->uvScale_;
    uvAngle_ = other->uvAngle_;
    angle_ = other->angle_;
    lit_ = other->lit_;
    alpha_ = other->alpha_;
    alphaScale_ = other->alphaScale_;
    colour_ = other->colour_;
    lifetime_ = other->lifetime_;
    positions_ = other->positions_;
    texCoords_ = other->texCoords_;
    indices_ = other->indices_;
    ei::SetVtable(this, kCopyVtableAddress);
    return this;
}

GroundPrint* GroundPrint::ConstructScorch(float x, float y, float halfWidth, float halfLength, float angle,
                                          float alphaScale, int life, int lifetime) {
    ConstructBase(1, life, alphaScale, lifetime, 0xFFFFFFFFu);
    position_ = {x, y};
    halfWidth_ = halfWidth;
    halfLength_ = halfLength;
    ei::SetVtable(this, kVtableAddress);
    Vector3 point{x, y, 0.0f};
    lit_ = camera::TheTerrain()->EdgeFade(&point);
    int quarter = RandomQuarter();
    uvScale_ = {kHalf, kHalf};
    uvOffset_.x = static_cast<float>(quarter & 1) * kHalf;
    uvOffset_.y = static_cast<float>(quarter >> 1) * kHalf;
    angle_ = angle;
    uvAngle_ = static_cast<float>(quarter) * HalfPi();
    SetTexture("fireprints");
    growth_ = 0.0f;
    return this;
}

int GroundPrint::TickImpl() {
    --life_;
    halfWidth_ = halfWidth_ + growth_;
    halfLength_ = halfLength_ + growth_;
    float t = static_cast<float>(static_cast<double>(lifetime_ - life_) / lifetime_);
    // max(t, 0) by FCOMP: an unordered t is kept.
    float spent = (t > 0.0f || x87::IsNaN(t)) ? t : 0.0f;
    alpha_ = (kOne - spent) * alphaScale_;
    return life_;
}

void GroundPrint::LoadImpl(CStream* stream) {
    Read(stream, &position_.x);
    Read(stream, &position_.y);
    Read(stream, &halfWidth_);
    Read(stream, &halfLength_);
    Read(stream, &growth_);
    Read(stream, &life_);
    char name[kNameCapacity];
    stream->ReadText(name, kNameCapacity);
    SetTexture(name);
    Read(stream, &uvOffset_.x);
    Read(stream, &uvOffset_.y);
    Read(stream, &uvScale_.x);
    Read(stream, &uvScale_.y);
    Read(stream, &uvAngle_);
    Read(stream, &angle_);
    Read(stream, &lit_);
    Read(stream, &alphaScale_);
    Read(stream, &lifetime_);
    Read(stream, &colour_);
    built_ = false;
}

void GroundPrint::SaveImpl(CStream* stream) {
    Write(stream, position_.x);
    Write(stream, position_.y);
    Write(stream, halfWidth_);
    Write(stream, halfLength_);
    Write(stream, growth_);
    Write(stream, life_);
    const char* name = texture_.name.data();
    stream->Write(name, static_cast<std::uint32_t>(std::strlen(name) + 1));
    Write(stream, uvOffset_.x);
    Write(stream, uvOffset_.y);
    Write(stream, uvScale_.x);
    Write(stream, uvScale_.y);
    Write(stream, uvAngle_);
    Write(stream, angle_);
    Write(stream, lit_);
    Write(stream, alphaScale_);
    Write(stream, lifetime_);
    Write(stream, colour_);
}

int GroundPrint::Draw() {
    if (!visible_) return 1;
    if (!built_) {
        // The quad at the height of the ground or the platform under the point.
        CWorldClient* world = ClientWorld();
        lib::AlignedArray<VertexPacket> quad;
        quad.Construct();
        Vector3 at{position_.x, position_.y, 0.0f};
        float z = static_cast<float>(world->platforms_.SurfaceHeight(&at, world->ClampedGroundHeight(position_.x, position_.y)));
        quad.SetSize(1);
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
        Vector3 above{position_.x, position_.y, z + kOne};
        ClientProjectDecalOriginal(world, nullptr, &down, &above, 0.0f, &quad.data, &positions_, &texCoords_, &indices_, 1, 1);

        // The texture coordinates turned about the tile's centre, then into the tile. The original's
        // loop is unrolled four times: the fourth vertex stores its u part to a float before scaling
        // it, the others their v part.
        float uc = static_cast<float>(x87::fcos(uvAngle_));
        float us = static_cast<float>(x87::fsin(uvAngle_));
        int vertices = positions_.size * 4;
        for (int i = 0; i < vertices; ++i) {
            DecalTexCoord& uv = texCoords_[i];
            double du = uv.u - kHalf;
            double dv = uv.v - kHalf;
            double u = (du * uc - dv * us) + kHalf;
            double v = (dv * uc + du * us) + kHalf;
            if ((i & 3) == 3) u = static_cast<float>(u);
            else v = static_cast<float>(v);
            uv.u = static_cast<float>(u * uvScale_.x + uvOffset_.x);
            uv.v = static_cast<float>(v * uvScale_.y + uvOffset_.y);
        }
        for (int i = 0; i < positions_.size; ++i)
            for (float& height : positions_[i].block[2]) height = height + kLift;
        built_ = true;
        quad.FreeData();
    }
    std::uint32_t alpha = static_cast<std::uint32_t>(x87::fistp(static_cast<float>(alpha_ * kAlphaRange)));
    colour_ = (colour_ & 0xFFFFFFu) | alpha << 24;
    CTextureDevice* device = RenderDeviceObject();
    device->BindNamedTexture(0, &texture_);
    return DeviceDrawFlatOriginal(device, nullptr, positions_.data, texCoords_.data, positions_.size, indices_.data,
                                  indices_.size, colour_, static_cast<std::uint32_t>(lit_) << 24, nullptr, 1.0f);
}

void GroundPrint::Destruct() {
    indices_.FreeData();
    texCoords_.FreeData();
    positions_.FreeData();
    if (texture_.texture) texture_.texture->Release();
    texture_.name.~string();
}

GroundPrint* GroundPrint::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// ---- Footprint ----------------------------------------------------------------------------------

Footprint* Footprint::Construct(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale,
                                int life, int lifetime, std::uint32_t tile, float scale, int visible, int kind) {
    ConstructBase(static_cast<std::uint8_t>(visible), life, alphaScale, lifetime, 0xFFFFFFFFu);
    kind_ = kind;
    scale_ = scale;
    position_ = {x, y};
    halfWidth_ = halfWidth;
    ei::SetVtable(this, kVtableAddress);
    halfLength_ = halfLength;
    Vector3 point{x, y, 0.0f};
    lit_ = camera::TheTerrain()->EdgeFade(&point);
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
        gamecrt::sprintf(name, "footprints%d", kind_ + 1);
        SetTexture(name);
    }
    growth_ = 0.0f;
    return this;
}

void Footprint::LoadImpl(CStream* stream) {
    GroundPrint::LoadImpl(stream);
    Read(stream, &scale_);
    Read(stream, &kind_);
}

void Footprint::SaveImpl(CStream* stream) {
    GroundPrint::SaveImpl(stream);
    Write(stream, scale_);
    Write(stream, kind_);
}

// ---- BloodPrint ---------------------------------------------------------------------------------

BloodPrint* BloodPrint::Construct(float x, float y, float halfSize, float fullSize, float alphaScale, int life,
                                  int lifetime, std::uint32_t tile, std::uint32_t colour, std::uint8_t fresh) {
    ConstructBase(1, life, alphaScale, lifetime, colour);
    position_ = {x, y};
    ei::SetVtable(this, kVtableAddress);
    halfWidth_ = halfSize;
    halfLength_ = halfSize;
    Vector3 point{x, y, 0.0f};
    lit_ = camera::TheTerrain()->EdgeFade(&point);
    int index = static_cast<int>(tile & 0xFFFF);
    int high = static_cast<int>(tile >> 16);
    uvOffset_.x = static_cast<float>(index & 1) * kQuarter + static_cast<float>(high & 1) * kHalf;
    uvScale_ = {kQuarter, kQuarter};
    uvOffset_.y = static_cast<float>(index >> 1) * kQuarter + static_cast<float>(high >> 1) * kHalf;
    uvAngle_ = static_cast<float>(RandomQuarter()) * HalfPi();
    int steps = x87::fistp(static_cast<float>(TwoPi() * kAngleMilli));
    angle_ = static_cast<float>(gamecrt::rand() * steps / 0x7FFF % steps) * kMilli;
    SetTexture("bloodprints");
    int spread = life / 2;
    if (spread > kMaxSpreadTicks) spread = kMaxSpreadTicks;
    spreadTicks_ = static_cast<std::int16_t>(spread);
    kind_ = high;
    fresh_ = fresh;
    float degrees = static_cast<float>(kSpinDegrees / static_cast<double>(life));
    float radiansPerDegree = static_cast<float>(Pi() * PerDegree());
    spin_ = degrees * radiansPerDegree;
    growth_ = static_cast<float>((fullSize - halfWidth_) / static_cast<double>(spreadTicks_));
    return this;
}

int BloodPrint::TickImpl() {
    if (--spreadTicks_ < 0) growth_ = 0.0f;
    else built_ = false;
    angle_ = angle_ + spin_;
    return GroundPrint::TickImpl();
}

void BloodPrint::LoadImpl(CStream* stream) {
    GroundPrint::LoadImpl(stream);
    Read(stream, &spin_);
    Read(stream, &spreadTicks_);
    Read(stream, &kind_);
    Read(stream, &fresh_);
}

void BloodPrint::SaveImpl(CStream* stream) {
    GroundPrint::SaveImpl(stream);
    Write(stream, spin_);
    Write(stream, spreadTicks_);
    Write(stream, kind_);
    Write(stream, fresh_);
}

// ---- The client world's prints ------------------------------------------------------------------

void CWorldClient::AddBloodPrint(float x, float y, float halfSize, float fullSize, std::uint32_t tile, int fresh) {
    if (!BloodPrintsOn()) return;
    const PrintRecord* entry = EntryAt(ThePrintsDatabase().blood, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, RainSnow()->mode != 0);
    if (!(look.alpha > 0.0f) || look.life <= 0 || look.lifetime <= 0) return;
    std::uint32_t colour = ((Channel(frameSun_.r) | 0x100) << 8 | Channel(frameSun_.g)) << 8 | Channel(frameSun_.b);
    alignas(BloodPrint) unsigned char storage[sizeof(BloodPrint)];
    auto* print = reinterpret_cast<BloodPrint*>(storage);
    print->Construct(x, y, halfSize, fullSize, look.alpha, look.life, look.lifetime, tile, colour,
                     static_cast<std::uint8_t>(fresh));
    stlp::list<BloodPrint>::Node* node;
    BloodPrintsInsert(&bloodPrints_, nullptr, &node, bloodPrints_.End(), print);
    print->Destruct();
}

int CWorldClient::AddFootprint(float x, float y, float halfWidth, float halfLength, float angle, std::uint32_t tile,
                               float scale, int visible, int bloodKind) {
    if (!FootprintsOn()) return -1;
    // Stepping into a blood print (not a fresh hit's) makes bloody footprints of its kind.
    bool inBlood = false;
    float reach = (halfWidth + halfLength) * kHalf;
    for (auto* node = bloodPrints_.First(); node != bloodPrints_.End(); node = node->next) {
        const BloodPrint& blood = node->data;
        float d2 = DistanceSquared2D(blood.position_, Vector2{x, y});
        float limit = (blood.halfWidth_ + reach) * (blood.halfWidth_ + reach);
        if (limit >= d2 && !blood.fresh_) {
            bloodKind = blood.kind_;
            inBlood = true;
            break;
        }
    }
    const PrintsDatabase& prints = ThePrintsDatabase();
    const PrintLook* look = nullptr;
    if (bloodKind == -1) {
        if (const PrintRecord2* entry = EntryAt(prints.footprints, this, x, y)) look = &LookOf(*entry, RainSnow()->mode != 0);
    } else {
        if (const PrintRecord* entry = EntryAt(prints.blood, this, x, y)) look = &LookOf(*entry, RainSnow()->mode != 0);
    }
    if (look && look->alpha > 0.0f && look->life > 0 && look->lifetime > 0) {
        alignas(Footprint) unsigned char storage[sizeof(Footprint)];
        auto* print = reinterpret_cast<Footprint*>(storage);
        print->Construct(x, y, halfWidth, halfLength, angle, look->alpha, look->life, look->lifetime, tile, scale,
                         visible, bloodKind);
        stlp::list<Footprint>::Node* node;
        FootprintsInsert(&footprints_, nullptr, &node, footprints_.End(), print);
        print->Destruct();
    }
    return inBlood ? bloodKind : -1;
}

void CWorldClient::AddScorchMark(float x, float y, float halfWidth, float halfLength, float angle, float alpha) {
    if (!ScorchMarksOn()) return;
    const PrintRecord* entry = EntryAt(ThePrintsDatabase().scorch, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, RainSnow()->mode != 0);
    if (!(alpha > 0.0f) || look.life <= 0 || look.lifetime <= 0) return;
    alignas(GroundPrint) unsigned char storage[sizeof(GroundPrint)];
    auto* print = reinterpret_cast<GroundPrint*>(storage);
    print->ConstructScorch(x, y, halfWidth, halfLength, angle, alpha, look.life, look.lifetime);
    stlp::list<GroundPrint>::Node* node;
    ScorchMarksInsert(&scorchMarks_, nullptr, &node, scorchMarks_.End(), print);
    print->Destruct();
}

// ---- Registration -------------------------------------------------------------------------------

// The virtual slots (rewrite-guide rule 4).

EI_REPLACE_METHOD(0x006CECD0, GroundPrint::TickImpl);
EI_REPLACE_METHOD(0x006CF4F0, GroundPrint::LoadImpl);
EI_REPLACE_METHOD(0x006CF370, GroundPrint::SaveImpl);
EI_REPLACE_METHOD(0x006CFE60, Footprint::LoadImpl);
EI_REPLACE_METHOD(0x006CFE10, Footprint::SaveImpl);
EI_REPLACE_METHOD(0x006CFA20, BloodPrint::TickImpl);
EI_REPLACE_METHOD(0x006CFB40, BloodPrint::LoadImpl);
EI_REPLACE_METHOD(0x006CFAC0, BloodPrint::SaveImpl);
EI_REPLACE_METHOD(0x006CEDE0, GroundPrint::Draw);
EI_REPLACE_METHOD(0x006CF960, GroundPrint::Destruct);
EI_REPLACE_METHOD(0x0056D080, GroundPrint::Destruct);
EI_REPLACE_METHOD(0x0056D400, GroundPrint::Destruct);
EI_REPLACE_METHOD(0x0056D680, GroundPrint::Destruct);
EI_REPLACE_METHOD(0x00575560, GroundPrint::DeletingDestruct);
EI_REPLACE_METHOD(0x00575580, GroundPrint::DeletingDestruct);
EI_REPLACE_METHOD(0x006CFEA0, GroundPrint::ConstructScorch);
EI_REPLACE_METHOD(0x00571FC0, GroundPrint::CopyConstruct);
EI_REPLACE_METHOD(0x006CFBA0, Footprint::Construct);
EI_REPLACE_METHOD(0x006CF660, BloodPrint::Construct);
EI_REPLACE_METHOD(0x0056CE90, CWorldClient::AddBloodPrint);
EI_REPLACE_METHOD(0x0056D150, CWorldClient::AddFootprint);
EI_REPLACE_METHOD(0x0056D4C0, CWorldClient::AddScorchMark);
