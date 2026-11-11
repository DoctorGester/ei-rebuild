// Ground prints: footprints, blood and scorch marks and the client world's functions that add them. A new print
// is built on the stack, copied into its list (sharing the empty geometry arrays) and destroyed; its prints.db
// entry (by the ground's surface class) gives the alpha and the life. See docs/notes/models.md ("Ground prints").
#include "game/terrain/ground_print.h"

#include "game/world/camera.h"
#include "engine/core/core_streams.h"
#include "game/effects/effects.h"
#include "engine/models/figure_mesh.h"
#include "game/app/singletons.h"
#include "game/terrain/ground_decal.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/app/options.h"
#include "engine/scene/light.h"
#include "game/terrain/terrain.h"
#include "game/world/world.h"
#include "engine/core/random.h"

#include "lib/containers.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "engine/data/chunk_archive.h"
#include "engine/data/database_file.h"
#include "engine/core/asset_text.h"
#include "lib/crt.h"
#include "engine/data/resource.h"
#include "engine/data/database_toml.h"
#include "lib/toml.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

// The database of the ground prints: built empty by the C++ runtime, loaded at start.
PrintsDatabase g_printsDatabase;

namespace {

constexpr int kTextureKind = static_cast<int>(TextureKind::Effects);                  // the texture manager's kind of the prints' textures
constexpr int kMaxSpreadTicks = 0x2D;
constexpr int kNameCapacity = 0x100;             // Load's name buffer
constexpr float kAtlasTileSize = 0.25f;   // a tile of the 4x4 texture atlas, in texture units
constexpr float kEighth = 0.125f;
constexpr float kSixteenth = 0.0625f;
constexpr float kAlphaRange = 255.0f;
constexpr float kLift = 0.01f;                   // the drawn quad above the ground
constexpr float kMilli = 0.001f;
constexpr float kAngleMilli = 1000.0f;
constexpr float kSpinDegrees = 30.0f;

constexpr float kTwoPi = /*007C8998*/ 6.2831855f;
// pi / 180.
constexpr float kPerDegree = 0.0055555557f;

int RandomQuarter() { return ClientRandBelow(4); }

// The prints.db entry for the ground at (x, y), or null.
template <typename Record>
const Record* EntryAt(const std::vector<Record>& table, WorldClient* world, float x, float y) {
    auto surface = static_cast<std::uint32_t>(world->terrain->SurfaceClass(x, y));
    return surface < table.size() ? &table[surface] : nullptr;
}

std::uint32_t Channel(float value) { return static_cast<std::uint32_t>(std::lround(value * kAlphaRange)); }

// Binds the print's texture to `stage`: one not looked up yet is found by its name among the texture manager's
// registered and cached textures (an empty name is not looked up) and kept, counted.
void BindNamedTexture(TextureDevice* device, int stage, NamedTexture* named) {
    if (!named->texture && named->name.size() != 0) {
        Texture* found = g_clientWorld.textureManager->FindTexture(named->name.data());
        named->texture.Assign(found);
    }
    device->BindSurface(stage, named->texture ? named->texture->surface.Get() : nullptr);
}

} // namespace

// ---- GroundPrint --------------------------------------------------------------------------------

GroundPrint::GroundPrint(std::uint8_t newVisible, int newLife, float newAlphaScale, int newLifetime) {
    visible = newVisible;
    life = static_cast<std::int16_t>(newLife);
    alphaScale = newAlphaScale;
    lifetime = newLifetime;
}

void GroundPrint::SetTexture(const char* name) {
    texture.name.assign(name, name + std::strlen(name));
    TextureManager* textures = g_clientWorld.textureManager.get();
    texture.texture.Assign(textures->FindTexture(name));
    if (texture.texture) return;
    texture.texture.Assign(textures->FindOrCreate(kTextureKind, name));
    textures->LoadTexture(texture.texture);
}

GroundPrint::GroundPrint(float x, float y, float newHalfWidth, float newHalfLength, float newAngle, float alphaScale, int life,
                         int lifetime)
    : GroundPrint(1, life, alphaScale, lifetime) {
    position = {x, y};
    halfWidth = newHalfWidth;
    halfLength = newHalfLength;
    Vector3 point{x, y, 0.0f};
    lit = g_clientWorld.terrain->EdgeFade(point);
    int quarter = RandomQuarter();
    uvScale = {0.5f, 0.5f};
    uvOffset.x = (quarter & 1) * 0.5f;
    uvOffset.y = (quarter >> 1) * 0.5f;
    angle = newAngle;
    uvAngle = quarter * kHalfPi;
    SetTexture("fireprints");
}

GroundPrint::GroundPrint(ByteStream* stream) : GroundPrint(1, 0, 0.0f, 0) { GroundPrint::Load(stream); }

void GroundPrint::Fade() {
    float t = static_cast<float>(lifetime - life) / lifetime;
    float spent = t > 0.0f ? t : 0.0f;
    alpha = (1.0f - spent) * alphaScale;
}

int GroundPrint::Tick() {
    --life;
    halfWidth += growth;
    halfLength += growth;
    Fade();
    return life;
}

void GroundPrint::Load(ByteStream* stream) {
    stream->ReadValue(position);
    stream->ReadValue(halfWidth);
    stream->ReadValue(halfLength);
    stream->ReadValue(growth);
    stream->ReadValue(life);
    char name[kNameCapacity];
    stream->ReadText(name, kNameCapacity);
    SetTexture(name);
    stream->ReadValue(uvOffset);
    stream->ReadValue(uvScale);
    stream->ReadValue(uvAngle);
    stream->ReadValue(angle);
    stream->ReadValue(lit);
    stream->ReadValue(alphaScale);
    stream->ReadValue(lifetime);
    built = false;
    Fade();
}

void GroundPrint::Save(ByteStream* stream) const {
    stream->WriteValue(position);
    stream->WriteValue(halfWidth);
    stream->WriteValue(halfLength);
    stream->WriteValue(growth);
    stream->WriteValue(life);
    stream->WriteString(texture.name.data());
    stream->WriteValue(uvOffset);
    stream->WriteValue(uvScale);
    stream->WriteValue(uvAngle);
    stream->WriteValue(angle);
    stream->WriteValue(lit);
    stream->WriteValue(alphaScale);
    stream->WriteValue(lifetime);
}

void GroundPrint::Draw() {
    if (!visible) return;
    if (!built) {
        // The quad at the height of the ground or the platform under the point.
        WorldClient* world = &g_clientWorld;
        std::vector<VertexPacket> quad;
        Vector3 at{position.x, position.y, 0.0f};
        float z = world->platforms.SurfaceHeight(at, world->ClampedGroundHeight(position.x, position.y));
        quad.resize(1);
        float c = lib::Cos(angle);
        float s = lib::Sin(angle);
        // Corners (+w, -l), (-w, -l), (-w, +l), (+w, +l) turned by the angle.
        const float across[4] = {halfWidth, -halfWidth, -halfWidth, halfWidth};
        const float along[4] = {-halfLength, -halfLength, halfLength, halfLength};
        VertexPacket& corners = quad[0];
        for (int i = 0; i < 4; ++i) {
            corners.block[0][i] = (across[i] * c + position.x) - along[i] * s;
            corners.block[1][i] = (across[i] * s + along[i] * c) + position.y;
            corners.block[2][i] = z;
            corners.block[3][i] = 1.0f;
        }
        Vector3 down{0.0f, 0.0f, -1.0f};
        Vector3 above{position.x, position.y, z + 1.0f};
        world->ProjectDecal(down, above, 0.0f, corners, &positions, &texCoords, &indices);

        // The texture coordinates turned about the tile's centre, then into the tile. The loop is
        // unrolled four times: the fourth vertex stores its u part to a float before scaling
        // it, the others their v part.
        float uc = lib::Cos(uvAngle);
        float us = lib::Sin(uvAngle);
        int vertices = lib::Count(positions) * 4;
        for (int i = 0; i < vertices; ++i) {
            TexCoord& uv = texCoords[i];
            float du = uv.u - 0.5f;
            float dv = uv.v - 0.5f;
            float u = (du * uc - dv * us) + 0.5f;
            float v = (dv * uc + du * us) + 0.5f;
            if ((i & 3) == 3) u = u;
            else v = v;
            uv.u = u * uvScale.x + uvOffset.x;
            uv.v = v * uvScale.y + uvOffset.y;
        }
        for (int i = 0; i < std::ssize(positions); ++i)
            for (float& height : positions[i].block[2]) height = height + kLift;
        built = true;
    }
    std::uint32_t fadeAlpha = static_cast<std::uint32_t>(std::lround(alpha * kAlphaRange));
    TextureDevice* device = g_sceneRenderer.device;
    BindNamedTexture(device, 0, &texture);
    device->DrawFlatBlocks(positions.data(), texCoords.data(), lib::Count(positions), indices.data(), lib::Count(indices),
                           (DayColour() & 0xFFFFFFu) | fadeAlpha << 24, static_cast<std::uint32_t>(lit) << 24, true);
}

std::uint32_t GroundPrint::DayColour() const { return 0xFFFFFFu; }

// ---- Footprint ----------------------------------------------------------------------------------

Footprint::Footprint(float x, float y, float newHalfWidth, float newHalfLength, float newAngle, float alphaScale, int life,
                     int lifetime, std::uint32_t tile, float newScale, int visible, int newKind)
    : GroundPrint(static_cast<std::uint8_t>(visible), life, alphaScale, lifetime) {
    kind = newKind;
    scale = newScale;
    position = {x, y};
    halfWidth = newHalfWidth;
    halfLength = newHalfLength;
    Vector3 point{x, y, 0.0f};
    lit = g_clientWorld.terrain->EdgeFade(point);
    // The tile's parts are below 2^16: converted as ints.
    int index = static_cast<int>(tile & 0xFFFF);
    uvOffset.x = (index & 7) * kEighth + static_cast<int>(tile >> 16) * kSixteenth;
    angle = newAngle;
    uvScale = {kSixteenth, kEighth};
    uvAngle = 0.0f;
    uvOffset.y = (index >> 3) * kEighth;
    if (kind == -1) {
        SetTexture("footprints");
    } else {
        char name[64];
        std::snprintf(name, sizeof(name), "footprints%d", kind + 1);
        SetTexture(name);
    }
    growth = 0.0f;
}

Footprint::Footprint(ByteStream* stream) : GroundPrint(0, 0, 0.0f, 0) { Footprint::Load(stream); }

void Footprint::Load(ByteStream* stream) {
    GroundPrint::Load(stream);
    stream->ReadValue(scale);
    stream->ReadValue(kind);
    stream->ReadValue(visible);
}

void Footprint::Save(ByteStream* stream) const {
    GroundPrint::Save(stream);
    stream->WriteValue(scale);
    stream->WriteValue(kind);
    stream->WriteValue(visible);
}

// ---- BloodPrint ---------------------------------------------------------------------------------

BloodPrint::BloodPrint(float x, float y, float halfSize, float fullSize, float alphaScale, int life, int lifetime,
                       std::uint32_t tile, std::uint8_t newFresh)
    : GroundPrint(1, life, alphaScale, lifetime) {
    position = {x, y};
    halfWidth = halfSize;
    halfLength = halfSize;
    Vector3 point{x, y, 0.0f};
    lit = g_clientWorld.terrain->EdgeFade(point);
    int index = static_cast<int>(tile & 0xFFFF);
    int high = static_cast<int>(tile >> 16);
    uvOffset.x = (index & 1) * kAtlasTileSize + (high & 1) * 0.5f;
    uvScale = {kAtlasTileSize, kAtlasTileSize};
    uvOffset.y = (index >> 1) * kAtlasTileSize + (high >> 1) * 0.5f;
    uvAngle = RandomQuarter() * kHalfPi;
    int steps = std::lround(kTwoPi * kAngleMilli);
    angle = ClientRandBelow(steps) * kMilli;
    SetTexture("bloodprints");
    int spread = life / 2;
    if (spread > kMaxSpreadTicks) spread = kMaxSpreadTicks;
    spreadTicks = static_cast<std::int16_t>(spread);
    kind = high;
    fresh = newFresh;
    float degrees = kSpinDegrees / life;
    float radiansPerDegree = kPi * kPerDegree;
    spin = degrees * radiansPerDegree;
    growth = (fullSize - halfWidth) / spreadTicks;
}

BloodPrint::BloodPrint(ByteStream* stream) : GroundPrint(1, 0, 0.0f, 0) { BloodPrint::Load(stream); }

int BloodPrint::Tick() {
    if (--spreadTicks < 0) growth = 0.0f;
    else built = false;
    angle += spin;
    return GroundPrint::Tick();
}

std::uint32_t BloodPrint::DayColour() const {
    LightColour sun, ambient;
    g_clientWorld.DaylightColours(&sun, &ambient);
    const float sunColour[3] = {sun.r, sun.g, sun.b};
    const std::uint32_t grey = Channel(std::clamp(Light::Luminance(sunColour), 0.0f, 1.0f));
    return (grey << 8 | grey) << 8 | grey;
}

void BloodPrint::Load(ByteStream* stream) {
    GroundPrint::Load(stream);
    stream->ReadValue(spin);
    stream->ReadValue(spreadTicks);
    stream->ReadValue(kind);
    stream->ReadValue(fresh);
}

void BloodPrint::Save(ByteStream* stream) const {
    GroundPrint::Save(stream);
    stream->WriteValue(spin);
    stream->WriteValue(spreadTicks);
    stream->WriteValue(kind);
    stream->WriteValue(fresh);
}

// ---- The client world's prints ------------------------------------------------------------------

void WorldClient::AddBloodPrint(float x, float y, float halfSize, float fullSize, std::uint32_t tile, int fresh) {
    const PrintRecord* entry = EntryAt(g_printsDatabase.blood, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, rainSnow.mode != 0);
    if (look.alpha <= 0.0f || look.life <= 0 || look.lifetime <= 0) return;
    bloodPrints.emplace_back(x, y, halfSize, fullSize, look.alpha, look.life, look.lifetime, tile,
                              static_cast<std::uint8_t>(fresh));
}

int WorldClient::AddFootprint(float x, float y, float halfWidth, float halfLength, float angle, std::uint32_t tile,
                               float scale, int visible, int bloodKind) {
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
    const PrintRecord* entry = EntryAt(g_printsDatabase.scorch, this, x, y);
    if (!entry) return;
    const PrintLook& look = LookOf(*entry, rainSnow.mode != 0);
    if (alpha <= 0.0f || look.life <= 0 || look.lifetime <= 0) return;
    scorchMarks.emplace_back(x, y, halfWidth, halfLength, angle, alpha, look.life, look.lifetime);
}

// ---- prints.db ----------------------------------------------------------------------------------

// The prints' records and their load from database.res, once at startup (as unit_db.cpp; docs/notes/models.md).

namespace {

using dbfile::LoadTable;
using dbfile::OpenDatabase;

// The chunks of prints.db.
constexpr int kBloodChunk = 1;
constexpr int kFootprintsChunk = 2;
constexpr int kScorchChunk = 3;

// Fields 1-3 of chunk `id`, four bytes each, if the chunk is present.
void TripleChunk(ChunkArchive* ar, int id, void* first, void* second, void* third) {
    if (!ar->EnterChunk(id)) return;
    ar->Field(1, first, 4);
    ar->Field(2, second, 4);
    ar->Field(3, third, 4);
    ar->LeaveChunk();
}
void TripleChunk(ChunkArchive* ar, int id, std::uint32_t* values) { TripleChunk(ar, id, &values[0], &values[1], &values[2]); }
void TripleChunk(ChunkArchive* ar, int id, PrintLook* look) { TripleChunk(ar, id, &look->alpha, &look->life, &look->lifetime); }

// Every record of `records` filed in `index` under its name (the index's operator[]: a record of
// a name seen before replaces the earlier one).
template <typename Record, typename Index, typename Name>
void FileRecords(std::vector<Record>& records, Index& index, Name name) {
    for (Record& record : records) index[name(record)] = &record;
}

} // namespace

void ChunkArchive::SerializePrints(int id, std::vector<PrintRecord>* records) {
    RecordChunks(this, id, records, &PrintRecord::Serialize);
}
void ChunkArchive::SerializeFootprints(int id, std::vector<PrintRecord2>* records) {
    RecordChunks(this, id, records, &PrintRecord2::Serialize);
}

PrintRecord::PrintRecord() : dryLook{}, wetLook{} {}
PrintRecord::PrintRecord(const PrintRecord& from) : name(from.name), dryLook(from.dryLook), wetLook(from.wetLook) {}
PrintRecord2::PrintRecord2() : dryLook{}, wetLook{}, unreadChunk{} {}
PrintRecord2::PrintRecord2(const PrintRecord2& from) : name(from.name), dryLook(from.dryLook), wetLook(from.wetLook) {
    std::memcpy(unreadChunk, from.unreadChunk, sizeof(unreadChunk));
}

void PrintRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &dryLook);
    TripleChunk(ar, 3, &wetLook);
}

void PrintRecord2::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    TripleChunk(ar, 2, &dryLook);
    TripleChunk(ar, 3, &wetLook);
    TripleChunk(ar, 10, unreadChunk);
}

int PrintsDatabase::Load(const std::string& folder) {
    std::string base = folder;
    MappedResource resource;
    OpenDatabase(resource, base);
    const RecordFile file = resource.FindRecord("prints.db");
    if (!file.Any()) game::FatalExit("Can't open terrain prints database\n");
    resource.CloseResource();
    Read(file);
    return 1;
}

void PrintsDatabase::Read(const RecordFile& file) {
    if (file.found) {
        MemoryStream stream;
        stream.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializePrints(kBloodChunk, &blood); });
        LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeFootprints(kFootprintsChunk, &footprints); });
        LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializePrints(kScorchChunk, &scorch); });
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    const auto name = [](auto& record) -> const std::string& { return record.name; };
    FileRecords(blood, bloodByName, name);
    FileRecords(footprints, footprintsByName, name);
    FileRecords(scorch, scorchByName, name);
}

// ---- prints.db as TOML ---------------------------------------------------------------------------

template <class Visit>
void Fields(PrintLook& r, Visit& v) {
    v("alpha", r.alpha);
    v("life", r.life);
    v("lifetime", r.lifetime);
}

template <class Visit>
void Fields(PrintRecord& r, Visit& v) {
    v("name", r.name);
    v("dry", r.dryLook);
    v("wet", r.wetLook);
}

template <class Visit>
void Fields(PrintRecord2& r, Visit& v) {
    v("name", r.name);
    v("dry", r.dryLook);
    v("wet", r.wetLook);
    v.Unread("unread", r.unreadChunk);
}

namespace {

constexpr const char* kPrints = "prints.db";
const auto kNameOf = [](const auto& record) -> const std::string& { return record.name; };

} // namespace

void PrintsDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["blood"], blood, "name", kNameOf, kPrints, path);
    dbtoml::MergeRecords(root["footprint"], footprints, "name", kNameOf, kPrints, path);
    dbtoml::MergeRecords(root["scorch"], scorch, "name", kNameOf, kPrints, path);
    dbtoml::CheckTables(root, {"blood", "footprint", "scorch"}, kPrints, path);
}

std::string PrintsDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "blood", blood, "name");
    dbtoml::WriteRecords(writer, "footprint", footprints, "name");
    dbtoml::WriteRecords(writer, "scorch", scorch, "name");
    return writer.text;
}
