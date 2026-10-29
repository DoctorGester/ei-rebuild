#pragma once

// Ground prints: footprints, blood and scorch marks lying on the ground. The client world keeps them by value
// in three lists (blood, footprints, scorch marks), ticks them (a print whose life reaches 0 is erased),
// draws them (Draw) and saves / loads them. A print is a textured quad around its point, rotated by `angle`,
// its texture tile picked by the uv offset and scale and turned by `uvAngle`; Draw lays it on the ground
// once (the decal projector) and draws it with an alpha that fades over the print's life. See
// docs/notes/models.md.

#include "ground_decal.h"   // DecalTexCoord

#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class CStream;
class CTexture;
struct VertexPacket;

// A texture by name. The device binds it by looking the texture up again by name.
struct NamedTexture {
    std::string name;
    Ref<CTexture> texture;
};

// A scorch mark and the base of the other two.
class GroundPrint {
public:
    // A scorch mark ("fireprints") at (x, y): a random quarter of the 2 x 2
    // texture, turned by the same quarter; white, lit as the terrain says.
    GroundPrint(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
                int lifetime);
    GroundPrint(const GroundPrint&) = delete;
    GroundPrint& operator=(const GroundPrint&) = delete;
    virtual ~GroundPrint() = default;

    // One tick: life - 1, both half sizes grow by `growth`, alpha = (1 - max(t, 0))
    // * alphaScale with t = (lifetime - life) / lifetime. Returns the life left.
    virtual int Tick();
    // Reads what Save writes (the texture looked up again by its name);
    // the geometry is made again (built cleared).
    virtual void Load(CStream* stream);
    // Writes the position, half sizes, growth, life (2 bytes), texture name (with its terminator), uv offset
    // and scale, both angles, lit (1 byte), alpha scale, lifetime and colour.
    virtual void Save(CStream* stream);

    // Draws the print when visible: the first time, lays the quad on the ground (z from the client world's
    // ground and platforms; the projector clips it to the terrain and the platforms under it) and turns its
    // texture coordinates; then the flat draw with the colour's alpha from alpha and the lit flag as the
    // specular alpha. Returns 1 when not visible, else the draw's result.
    int Draw();

protected:
    // The members the three constructors share before their own parts: everything cleared, the
    // texture name empty, `visible`, life and alpha scale, lifetime and colour set.
    GroundPrint(std::uint8_t visible, int life, float alphaScale, int lifetime, std::uint32_t colour);
    // The texture `name`, looked up in the texture manager or made and loaded (kind 6).
    void SetTexture(const char* name);

public:
    bool built;                    // the ground geometry is made
    std::uint8_t visible;           // drawn only when nonzero (a footprint: the caller's low byte)
    Vector2 position;
    float halfWidth;
    float halfLength;
    float growth;                   // added to both half sizes every tick
    std::int16_t life;              // ticks left
    NamedTexture texture;
    Vector2 uvOffset;               // the tile in the texture
    Vector2 uvScale;
    float uvAngle;                  // the texture's turn about its tile's centre
    float angle;                    // the quad's turn
    std::uint8_t lit;               // the terrain's point test; the specular alpha
    float alpha;
    float alphaScale;
    std::uint32_t colour;           // its alpha byte is set from alpha every draw
    int lifetime;
    std::vector<VertexPacket> positions;      // the ground geometry (blocks of four)
    std::vector<DecalTexCoord> texCoords;     // one per vertex
    std::vector<std::uint16_t> indices;
};

class Footprint : public GroundPrint {
public:
    // A footprint at (x, y): `tile` picks the texture's eighth (low 3 bits across, bits 3..15 down; bit 16
    // the left foot's half); `kind` -1 "footprints", else "footprints<kind + 1>" (a blood print's kind).
    Footprint(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
              int lifetime, std::uint32_t tile, float scale, int visible, int kind);

    void Load(CStream* stream) override;   // GroundPrint's, then scale, kind
    void Save(CStream* stream) override;   // the same order

    float scale;                    // the unit's footprint scale (only saved)
    int kind;                       // -1, or the blood print's kind it was made with
};

// A blood print. Spreads for half its life (at most 45 ticks), turning.
class BloodPrint : public GroundPrint {
public:
    // A blood print ("bloodprints") of `halfSize` growing to `fullSize` in
    // min(life / 2, 45) ticks: `tile` bits 0 / 16 pick the quarter across, bits 1..15 / 17.. down;
    // tile >> 16 is the kind footprints take from it. A random quarter turn and a random start
    // angle (ClientRand() twice).
    BloodPrint(float x, float y, float halfSize, float fullSize, float alphaScale, int life, int lifetime,
               std::uint32_t tile, std::uint32_t colour, std::uint8_t fresh);

    // While spreading (spreadTicks counts down) the geometry is made again every
    // tick, after that the growth stops; the angle turns by spin; then GroundPrint's tick.
    int Tick() override;
    void Load(CStream* stream) override;   // GroundPrint's, then spin, spread, kind, fresh
    void Save(CStream* stream) override;

    float spin;                     // radians per tick: 30 / life degrees
    std::int16_t spreadTicks;
    int kind;                       // tile >> 16
    std::uint8_t fresh;             // a hit's blood (not a death's): footprints ignore it
};
