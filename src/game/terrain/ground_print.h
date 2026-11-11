#pragma once

// Ground prints: footprints, blood and scorch marks lying on the ground. The client world keeps them by value
// in three vectors (blood, footprints, scorch marks), ticks them (a print whose life reaches 0 is erased) and
// draws them (Draw). The original's save holds no prints; ours keeps them in the side file beside a save (Save, and the
// constructors from a stream: client_visuals.h's Prints section). A print is a textured quad
// around its point, rotated by `angle`, its texture tile picked by the uv offset and scale and turned by
// `uvAngle`; Draw lays it on the ground once (the decal projector) and draws it with an alpha that fades over
// the print's life. prints.db's records (PrintsDatabase, loaded at startup) give each kind of print its alpha and
// life. See docs/notes/models.md.

#include "game/terrain/ground_decal.h"   // DecalBuilder

#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

class ByteStream;
class ChunkArchive;
class Texture;
struct RecordFile;
namespace toml {
class Value;
}

// A texture by name, looked up by its name when the print first binds it (ground_print.cpp).
struct NamedTexture {
    std::string name;
    HeldRef<Texture> texture;   // a HeldRef: the client world's print vectors move the prints
};

// A scorch mark and the base of the other two.
class GroundPrint {
public:
    // A scorch mark ("fireprints") at (x, y): a random quarter of the 2 x 2
    // texture, turned by the same quarter; white, lit as the terrain says.
    GroundPrint(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
                int lifetime);
    // A scorch mark read back from what Save wrote (Load), shown.
    explicit GroundPrint(ByteStream* stream);
    GroundPrint(const GroundPrint&) = delete;
    GroundPrint& operator=(const GroundPrint&) = delete;
    GroundPrint(GroundPrint&&) noexcept = default;   // the client world's print vectors move them
    GroundPrint& operator=(GroundPrint&&) noexcept = default;
    virtual ~GroundPrint() = default;

    // One tick: life - 1, both half sizes grow by `growth`, alpha = (1 - max(t, 0))
    // * alphaScale with t = (lifetime - life) / lifetime. Returns the life left.
    virtual int Tick();
    // Reads what Save writes (the texture looked up again by its name); the geometry is made again (built cleared)
    // and the alpha is the one the life left gives, as Tick sets it.
    virtual void Load(ByteStream* stream);
    // Writes the position, half sizes, growth, life (2 bytes), texture name (with its terminator), uv offset
    // and scale, both angles, lit (1 byte), alpha scale and lifetime.
    virtual void Save(ByteStream* stream) const;

    // Draws the print when visible: the first time, lays the quad on the ground (z from the client world's
    // ground and platforms; the projector clips it to the terrain and the platforms under it) and turns its
    // texture coordinates; then the flat draw in DayColour with the alpha from alpha and the lit flag as
    // the specular alpha (the renderer lights it as the ground under it).
    void Draw();
    // The print's colour in the client world's daylight colours (WorldClient::DaylightColours), rgb: white.
    virtual std::uint32_t DayColour() const;

protected:
    // The members the three constructors share before their own parts: everything cleared, the
    // texture name empty, `visible`, life and alpha scale and lifetime set.
    GroundPrint(std::uint8_t visible, int life, float alphaScale, int lifetime);
    // The texture `name`, looked up in the texture manager or made and loaded (kind 6).
    void SetTexture(const char* name);
    // alpha = (1 - max(t, 0)) * alphaScale with t = (lifetime - life) / lifetime.
    void Fade();

public:
    bool built = false;            // the ground geometry is made
    std::uint8_t visible = 0;       // drawn only when nonzero (a footprint: the caller's low byte)
    Vector2 position;
    float halfWidth = 0.0f;
    float halfLength = 0.0f;
    float growth = 0.0f;            // added to both half sizes every tick
    std::int16_t life = 0;          // ticks left
    NamedTexture texture;
    Vector2 uvOffset;               // the tile in the texture
    Vector2 uvScale;
    float uvAngle = 0.0f;           // the texture's turn about its tile's centre
    float angle = 0.0f;             // the quad's turn
    std::uint8_t lit = 0xFF;        // the terrain's point test; the specular alpha
    float alpha = 1.0f;
    float alphaScale = 0.0f;
    int lifetime = 0;
    std::vector<VertexPacket> positions;      // the ground geometry (blocks of four)
    std::vector<TexCoord> texCoords;     // one per vertex
    std::vector<std::uint16_t> indices;
};

class Footprint : public GroundPrint {
public:
    // A footprint at (x, y): `tile` picks the texture's eighth (low 3 bits across, bits 3..15 down; bit 16
    // the left foot's half); `kind` -1 "footprints", else "footprints<kind + 1>" (a blood print's kind).
    Footprint(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
              int lifetime, std::uint32_t tile, float scale, int visible, int kind);
    // A footprint read back from what Save wrote (Load).
    explicit Footprint(ByteStream* stream);

    // GroundPrint's, then scale, kind and (ours) the visible byte: the original's Save had no caller and left it
    // out, so a footprint not yet seen would come back shown.
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) const override;   // the same order

    float scale = 0.0f;             // the unit's footprint scale (only saved)
    int kind = 0;                   // -1, or the blood print's kind it was made with
};

// A blood print. Spreads for half its life (at most 45 ticks), turning.
class BloodPrint : public GroundPrint {
public:
    // A blood print ("bloodprints") of `halfSize` growing to `fullSize` in
    // min(life / 2, 45) ticks: `tile` bits 0 / 16 pick the quarter across, bits 1..15 / 17.. down;
    // tile >> 16 is the kind footprints take from it. A random quarter turn and a random start
    // angle (ClientRand() twice).
    BloodPrint(float x, float y, float halfSize, float fullSize, float alphaScale, int life, int lifetime,
               std::uint32_t tile, std::uint8_t fresh);
    // A blood print read back from what Save wrote (Load), shown.
    explicit BloodPrint(ByteStream* stream);

    // While spreading (spreadTicks counts down) the geometry is made again every
    // tick, after that the growth stops; the angle turns by spin; then GroundPrint's tick.
    int Tick() override;
    // The grey of the daylight sun's luminance.
    std::uint32_t DayColour() const override;
    void Load(ByteStream* stream) override;   // GroundPrint's, then spin, spread, kind, fresh
    void Save(ByteStream* stream) const override;

    float spin = 0.0f;              // radians per tick: 30 / life degrees
    std::int16_t spreadTicks = 0;
    int kind = 0;                   // tile >> 16
    std::uint8_t fresh = 0;         // a hit's blood (not a death's): footprints ignore it
};

static_assert(std::is_nothrow_move_constructible_v<Footprint> && std::is_nothrow_move_constructible_v<BloodPrint>);

// What chunks 2 and 3 of both prints.db record types hold: a print's alpha and its life and lifetime in ticks
// (chunk 3 while it rains or snows).
struct PrintLook {
    float alpha = 0.0f;
    int life = 0;
    int lifetime = 0;
};

// A record of prints.db (terrain footprints).
struct PrintRecord {
    PrintRecord();
    PrintRecord(const PrintRecord& from);
    PrintRecord& operator=(const PrintRecord&) = delete;

    virtual void Serialize(ChunkArchive* ar);

    std::string name;
    PrintLook dryLook;           // chunk 2
    PrintLook wetLook;           // chunk 3 (while it rains or snows)
};

// The second record type of prints.db.
struct PrintRecord2 {
    PrintRecord2();
    PrintRecord2(const PrintRecord2& from);
    PrintRecord2& operator=(const PrintRecord2&) = delete;

    virtual void Serialize(ChunkArchive* ar);

    std::string name;
    PrintLook dryLook;           // chunk 2
    PrintLook wetLook;           // chunk 3 (while it rains or snows)
    std::uint32_t unreadChunk[3] = {};  // chunk 10
};

inline const PrintLook& LookOf(const PrintRecord& r, bool second) { return second ? r.wetLook : r.dryLook; }
inline const PrintLook& LookOf(const PrintRecord2& r, bool second) { return second ? r.wetLook : r.dryLook; }

// prints.db (loaded at startup), indexed by the ground's surface class (Terrain::SurfaceClass): chunk 1
// blood and bloody footprints, chunk 2 footprints, chunk 3 scorch marks; each table is followed by a
// hash_map by name (filled by the load, never read).
struct PrintsDatabase {
    // "prints.db" of `folder` + "database.res" (Rebirth-patched): the three tables, each read through an
    // archive of its own and filed under its records' names. 1; an archive or file that cannot be opened ends the
    // game.
    int Load(const std::string& folder);
    // Load's work on the file found: the tables of the original, then of each TOML file over it (MergeToml), filed.
    void Read(const RecordFile& file);

    // TOML (docs/notes/mods.md, "Records as TOML"): [[blood]], [[footprint]] and [[scorch]] tables, each a record:
    // name, dry = { alpha, life, lifetime }, wet = { ... }, a footprint's unread = [three numbers] (chunk 10). Merge:
    // a record replaces the first of its name in its table (not one this file replaced already), or is appended (a
    // table's index is the ground's surface class); an unknown key is logged (with `path`).
    void MergeToml(const toml::Value& root, const std::string& path);
    std::string ToToml() const;

    std::vector<PrintRecord> blood;
    std::unordered_map<std::string, PrintRecord*> bloodByName;
    std::vector<PrintRecord2> footprints;
    std::unordered_map<std::string, PrintRecord2*> footprintsByName;
    std::vector<PrintRecord> scorch;
    std::unordered_map<std::string, PrintRecord*> scorchByName;
};
extern PrintsDatabase g_printsDatabase;
