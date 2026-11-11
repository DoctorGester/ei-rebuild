#pragma once

// Weather and visibility fog: the per-frame simulation parts beside the particles (particle_source.h, included here).
// - RainSnow (the world's rain/snow layer; "RainSnow"): fades its density and the rain sound.
// - VisibilityFog ("VisibilityFog"): a hash_map of fog cells, one per visible map square; each
// cell fades in while visible and out once not, and is dropped when nearly transparent.

#include "game/effects/particle_source.h"
#include "game/scene/clip.h"   // TLVertex
#include "game/core/core_streams.h"
#include "game/ai/search_grid.h"

#include "lib/containers.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace weather {
enum class Kind;   // weather_field.h
}

// The world's rain/snow layer.
class RainSnow final : public EngineObject {
public:
    RainSnow();
    ~RainSnow() override;
    void Create() override {}
    void Destroy() override { Clear(); }
    const char* ClassName() override;
    ClassId GetClassId() override;

    int Draw(SceneRenderer* renderer);
    void UpdateFade(float now);
    void SetWind(const Vector3& direction, float strength);
    void Clear();
    // A running loop stopped unless `newMode` is 0; rain starts the looping "nature\rain\circle.wav"; the fade
    // towards `newMode` starts now (the world's tick) over `duration` (0: at the next step). Called by the weather
    // messages.
    void Start(int newMode, float duration);
    // The rain's or the snow's particles drawn (weather_particles.cpp): the field of weather_field.h around the view,
    // `density` of 64 of them shown, each built on the screen (a raindrop a streak along its motion on the white
    // texture, a snowflake a square of one of `texture`'s flake pictures) and drawn as pre-transformed triangles with
    // the device's current state.
    void DrawParticles(SceneRenderer* renderer, weather::Kind kind);
    // The vertices built so far drawn (unless the harness leaves the particles out) and emptied.
    void Flush(SceneRenderer* renderer);
    // The terrain's ground or water height at (x, y) for the particles: between the heights at the whole units around
    // it, kept in `ground` (weather_particles.cpp).
    float GroundAt(float x, float y);

    Ref<Texture> texture;          // "RainSnow": the snowflakes' pictures (taken at the first snow drawn)
    class WorldClient* world;      // the client world (set after construction)
    int sound = 0;                  // rain loop handle in the sound manager (0: none)
    // The draw's work arrays: a batch of particles' vertices and their triangles.
    std::vector<TLVertex> vertices;
    std::vector<std::uint16_t> indices;
    int flushed = 0;   // the vertices drawn so far by this draw
    // The terrain's surface heights at whole units around the view, kept from frame to frame: a point's slot is its
    // place modulo the table's side (GroundAt). Emptied when the world's terrain changes.
    struct GroundPoint {
        int x = std::numeric_limits<int>::min();   // the unit the height is of (none yet)
        int y = 0;
        float height = 0.0f;
    };
    std::vector<GroundPoint> ground;
    const void* groundOf = nullptr;   // the terrain `ground` holds heights of
    // Where the wind has carried the particles (weather::FieldView::carry), up to the world time `carriedTo`.
    Vector3 carry;
    double carriedTo = 0.0;
    bool carrying = false;   // `carriedTo` holds a frame's time
    Vector3 wind;
    int mode = 0;                   // 0 off, 1 rain, 2 snow
    int targetMode = 0;
    int density = 0;                // 0..64
    float fadeStart = 0.0f;         // world time
    float fadeDuration = 0.0f;      // 0: no fade running
};

// A fog cell (the hash_map value; the key is (y << 16) + x of the map square).
struct FogCell {
    std::int16_t x = 0;             // map square
    std::int16_t y = 0;
    Vector3 pos;                    // jittered centre, z = ground + 0.5
    float alpha = 0.0f;
    std::uint8_t visible = 0;
    std::int8_t frame = 0;          // 0..14, picks the texture tile
};

// The texture coordinates of the fog texture's 4 x 4 tiles (16 records; the fog manager's Create fills them,
// the fog's draw hands them to the renderer): a tile's u range, and its v range counted down from 1 a row.
struct FogTile {
    float u0 = 0.0f;  // column / 4
    float v1 = 0.0f;  // v0 + 1/4
    float u1 = 0.0f;  // u0 + 1/4
    float v0 = 0.0f;  // 1 - (row + 1) / 4
};
inline FogTile g_fogTiles[16] = /*007ABA58*/ {};

// The squares currently seen: rows of bytes, nonzero = visible.
struct FogGrid {
    void Clear();                         // no origin (0x7FFFFFFF), 1 x 1, zeroed
    // x0, y0, width, height (dwords), then the cells; reading makes the cells again when the size changed.
    void Read(MemoryStream* stream);
    void Write(MemoryStream* stream);
    // Grows the grid to cover (x - n .. x + n, y - n .. y + n); the first call (x0 = 0x7FFFFFFF) sizes and
    // clears it.
    void Grow(int x, int y, int n);

    int x0 = 0;                     // map cell of cells[0][0]; 0x7FFFFFFF before the first Grow
    int y0 = 0;
    SearchGrid<std::uint8_t> cells; // width * height bytes and the row table (operator new)
};

class VisibilityFog final : public EngineObject {
public:
    using CellMap = lib::HashMap<std::uint32_t, FogCell>;   // Draw hands the cells' sprites over in STLport's order

    VisibilityFog();
    ~VisibilityFog() override;
    void Create() override;
    void Destroy() override { ClearCells(); }
    bool RunTick() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    void ComputeBounds();
    void Reveal(const FogGrid* grid);
    // Every cell freed (nodes back to the pool), the bounds invalid.
    void ClearCells();
    // The cells as sprites through the scene renderer; 1 without cells or when the bounds are out of view.
    int Draw(class SceneRenderer* renderer);

    ClipBounds bounds = {};         // flags 3 once computed; the box relative to the centre
    CellMap cells;
    std::uint8_t boundsValid = 0;
};
