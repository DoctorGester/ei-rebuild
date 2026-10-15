#pragma once

// Ground prints (006CECD0-006CFEA0): footprints, blood and scorch marks lying on the ground. The
// client world keeps them by value in three lists (CWorldClient +0x1B8 blood, +0x1BC footprints,
// +0x1C0 scorch marks; 0056CE90 / 0056D150 / 0056D4C0 add them), ticks them (slot 0; a print
// whose life reaches 0 is erased), draws them (Draw) and saves / loads them (slots 1 / 2). A print
// is a textured quad around its point, rotated by `angle_`, its texture tile picked by the uv
// offset and scale and turned by `uvAngle_`; Draw lays it on the ground once (the decal projector
// 0056C750) and draws it with an alpha that fades over the print's life. See docs/notes/models.md.

#include "lib/containers.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>

class CStream;
class CTexture;
struct VertexPacket;    // figure_mesh.h
struct DecalTexCoord;   // ground_decal.h

// A texture by name (0x10 bytes; constructed in place by 006CF900, an empty name with room for 8
// bytes). The device binds it with 004EB1D0, which looks the texture up again by name.
struct NamedTexture {
    NamedTexture* Copy(const NamedTexture* other);
    stlp::string name;      // +0x00
    CTexture* texture;      // +0x0C counted
};
static_assert(sizeof(NamedTexture) == 0x10);

// A scorch mark (vtable 0073F76C; the copies the lists make get 0073F778, with the same slots)
// and the base of the other two. 0x8C bytes.
class __declspec(novtable) GroundPrint {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F76C);
    // The vtable of the copies the lists' nodes start from (CopyConstruct; the same slots).
    static constexpr std::uintptr_t kCopyVtableAddress = std::uintptr_t(0x0073F778);

    // 0: 006CECD0. One tick: life - 1, both half sizes grow by `growth_`, alpha = (1 - max(t, 0))
    // * alphaScale with t = (lifetime - life) / lifetime. Returns the life left.
    virtual int Tick();
    int TickImpl();   // body of the slot above (registered by member pointer)
    // 1: 006CF4F0 (RET 4). Reads what Save writes (the texture looked up again by its name);
    // the geometry is made again (built_ cleared).
    virtual void Load(CStream* stream);
    void LoadImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    // 2: 006CF370 (RET 4). Writes the position, half sizes, growth, life (2 bytes), texture name
    // (with its terminator), uv offset and scale, both angles, lit (1 byte), alpha scale,
    // lifetime and colour.
    virtual void Save(CStream* stream);
    void SaveImpl(CStream* stream);   // body of the slot above (registered by member pointer)

    // 006CFEA0 (RET 0x20). A scorch mark ("fireprints") at (x, y): a random quarter of the 2 x 2
    // texture, turned by the same quarter; white, lit as the terrain says. Returns this.
    GroundPrint* ConstructScorch(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale,
                                 int life, int lifetime);
    // 006CEDE0. Draws the print when visible: the first time, lays the quad on the ground (z from
    // the client world's ground and platforms; the projector 0056C750 clips it to the terrain and
    // the platforms under it) and turns its texture coordinates; then the flat draw 004EA4A0 with
    // the colour's alpha from alpha_ and the lit flag as the specular alpha. Returns 1 when not
    // visible, else the draw's result.
    int Draw();
    // 006CF960. The destructor (only reached from the constructors' unwinding): the arrays freed,
    // the texture released, the name freed. The client world's lists have three more copies of it
    // (0056D080 blood, 0056D400 footprints, 0056D680 scorch marks; S13).
    void Destruct();
    // 00575560 / 00575580 (RET 4; S13): the footprint and scorch mark lists' scalar deleting
    // destructors: Destruct, the print freed when bit 0 of `flags` is set; returns this.
    GroundPrint* DeletingDestruct(unsigned flags);

    // 00571FC0 (RET 4). The copy constructor the lists' node copies start with (0056F880 /
    // 0056F940 / 0056F9F0, which then copy their own members and store their vtable): every member
    // copied, the texture name into a block of its own and the texture counted (NamedTexture::Copy),
    // the three geometry arrays' headers as they are (the copy shares their blocks); then
    // kCopyVtableAddress. Returns this.
    GroundPrint* CopyConstruct(const GroundPrint* other);

    // The members the three constructors share before their own parts: everything cleared, the
    // texture name empty, `visible`, life and alpha scale, lifetime and colour set.
    void ConstructBase(std::uint8_t visible, int life, float alphaScale, int lifetime, std::uint32_t colour);
    // The texture `name`, looked up in the texture manager or made and loaded (kind 6).
    void SetTexture(const char* name);

    bool built_;                    // +0x04 the ground geometry is made
    std::uint8_t visible_;          // +0x05 drawn only when nonzero (a footprint: the caller's low byte)
    Vector2 position_;              // +0x08
    float halfWidth_;               // +0x10
    float halfLength_;              // +0x14
    float growth_;                  // +0x18 added to both half sizes every tick
    std::int16_t life_;             // +0x1C ticks left
    NamedTexture texture_;          // +0x20
    Vector2 uvOffset_;              // +0x30 the tile in the texture
    Vector2 uvScale_;               // +0x38
    float uvAngle_;                 // +0x40 the texture's turn about its tile's centre
    float angle_;                   // +0x44 the quad's turn
    std::uint8_t lit_;              // +0x48 the terrain's point test (006CB020); the specular alpha
    float alpha_;                   // +0x4C
    float alphaScale_;              // +0x50
    std::uint32_t colour_;          // +0x54 its alpha byte is set from alpha_ every draw
    int lifetime_;                  // +0x58
    lib::AlignedArray<VertexPacket> positions_;     // +0x5C the ground geometry (blocks of four)
    lib::AlignedArray<DecalTexCoord> texCoords_;    // +0x6C one per vertex
    lib::AlignedArray<std::uint16_t> indices_;      // +0x7C
};
static_assert(offsetof(GroundPrint, position_) == 0x08 && offsetof(GroundPrint, life_) == 0x1C);
static_assert(offsetof(GroundPrint, texture_) == 0x20 && offsetof(GroundPrint, uvOffset_) == 0x30);
static_assert(offsetof(GroundPrint, lit_) == 0x48 && offsetof(GroundPrint, colour_) == 0x54);
static_assert(offsetof(GroundPrint, positions_) == 0x5C && offsetof(GroundPrint, indices_) == 0x7C);
static_assert(sizeof(GroundPrint) == 0x8C);

// A footprint (vtable 0073F760; slot 0 is GroundPrint's). 0x94 bytes.
class __declspec(novtable) Footprint : public GroundPrint {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F760);

    void Load(CStream* stream) override;   // 1: 006CFE60 (RET 4): GroundPrint's, then scale_, kind_
    void LoadImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    void Save(CStream* stream) override;   // 2: 006CFE10 (RET 4): the same order
    void SaveImpl(CStream* stream);   // body of the slot above (registered by member pointer)

    // 006CFBA0 (RET 0x30). A footprint at (x, y): `tile` picks the texture's eighth (low 3 bits
    // across, bits 3..15 down; bit 16 the left foot's half); `kind` -1 "footprints", else
    // "footprints<kind + 1>" (a blood print's kind). Returns this.
    Footprint* Construct(float x, float y, float halfWidth, float halfLength, float angle, float alphaScale, int life,
                         int lifetime, std::uint32_t tile, float scale, int visible, int kind);

    float scale_;                   // +0x8C the unit's footprint scale (only saved)
    int kind_;                      // +0x90 -1, or the blood print's kind it was made with
};
static_assert(offsetof(Footprint, scale_) == 0x8C && sizeof(Footprint) == 0x94);

// A blood print (vtable 0073F754). Spreads for half its life (at most 45 ticks), turning. 0x9C
// bytes.
class __declspec(novtable) BloodPrint : public GroundPrint {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F754);

    // 0: 006CFA20. While spreading (spreadTicks_ counts down) the geometry is made again every
    // tick, after that the growth stops; the angle turns by spin_; then GroundPrint's tick.
    int Tick() override;
    int TickImpl();   // body of the slot above (registered by member pointer)
    void Load(CStream* stream) override;   // 1: 006CFB40 (RET 4): GroundPrint's, then spin, spread, kind, fresh
    void LoadImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    void Save(CStream* stream) override;   // 2: 006CFAC0 (RET 4)
    void SaveImpl(CStream* stream);   // body of the slot above (registered by member pointer)

    // 006CF660 (RET 0x28). A blood print ("bloodprints") of `halfSize` growing to `fullSize` in
    // min(life / 2, 45) ticks: `tile` bits 0 / 16 pick the quarter across, bits 1..15 / 17.. down;
    // tile >> 16 is the kind footprints take from it. A random quarter turn and a random start
    // angle (rand() twice). Returns this.
    BloodPrint* Construct(float x, float y, float halfSize, float fullSize, float alphaScale, int life, int lifetime,
                          std::uint32_t tile, std::uint32_t colour, std::uint8_t fresh);

    float spin_;                    // +0x8C radians per tick: 30 / life degrees
    std::int16_t spreadTicks_;      // +0x90
    int kind_;                      // +0x94 tile >> 16
    std::uint8_t fresh_;            // +0x98 a hit's blood (not a death's): footprints ignore it
};
static_assert(offsetof(BloodPrint, spin_) == 0x8C && offsetof(BloodPrint, kind_) == 0x94);
static_assert(offsetof(BloodPrint, fresh_) == 0x98 && sizeof(BloodPrint) == 0x9C);
