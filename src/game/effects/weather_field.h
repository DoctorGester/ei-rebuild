#pragma once

// The rain's and the snow's particles as a field around the view: where each one is at a moment and how much of it
// shows. Client-only and without state of its own: every particle's place follows from its number, the world's time
// and the wind carried so far, so a frame shows the same field as the last one moved on by the time between them.
// weather_particles.cpp draws it; docs/notes/renderer.md, section 12, "Weather".
//
// The particles fill a box of kBoxSide x kBoxSide x kBoxHeight units that repeats over the world: a particle's anchor
// is its own point in the box, carried by the wind and its fall, taken to the copy of the box around the field's
// centre; a snowflake drifts about its anchor. A particle leaves the box on one side as it comes back in on the other,
// so its alpha fades to nothing at the box's walls (and near the eye): it never jumps or vanishes while it is seen.
//
// The particles are numbered bin by bin, so that a draw passes over the bins out of view whole (Field::BinBounds): the
// box is cut into kColumns x kColumns upright columns and kLayers layers by where a particle's own point lies, and each
// particle falls at one of kFallGroups speeds, so a bin's particles keep together in a box kBoxSide / kColumns wide and
// kBoxHeight / kLayers high (and the snow's drift on its sides) wherever the wind and the time take them.

#include "lib/vecmath.h"

namespace weather {

enum class Kind { Rain, Snow };

// The units the box spans: the horizontal sides and the height.
constexpr float kBoxSide = 80.0f;
constexpr float kBoxHeight = 40.0f;
// The bins: the columns along each horizontal side, the layers, the fall speeds.
constexpr int kColumns = 8;
constexpr int kLayers = 4;
constexpr int kFallGroups = 4;
constexpr int kBins = kColumns * kColumns * kLayers * kFallGroups;
// The farthest the snow's drift takes a flake from its anchor (the drift's amplitudes on x and y, 0.5 and 0.4).
constexpr float kMostDrift = 0.65f;

// The view the field is placed by.
struct FieldView {
    double time = 0.0;    // the world's ticks and their fraction
    Vector3 centre;         // the box's centre
    Vector3 eye;            // the camera
    Vector3 carry;          // where the wind has carried the air so far (units, each axis within one box)
    Vector3 wind;           // the wind (units per tick)
    float density = 1.0f; // the share of the particles shown (the layer's fade: density / 64)
};

// A particle at the view's moment.
struct Particle {
    Vector3 position;
    Vector3 velocity;       // units per tick: the wind and its fall (rain) or its fall and drift (snow)
    float alpha = 0.0f;   // 0..1: the box's walls, the eye's nearness and the density
    float size = 0.0f;    // the flake's radius or the drop's width, in units
    float shade = 0.0f;   // 0..1: its own brightness
    int frame = 0;        // the snowflake's picture: 0..3
    float spinCos = 1.0f; // the snowflake's turn on the screen (its cosine and sine): a slow spin
    float spinSin = 0.0f;
};

// The particles of a bin: the indices first .. end - 1.
struct BinRange {
    int first = 0;
    int end = 0;
};

// A box of world points.
struct Bounds {
    Vector3 low;
    Vector3 high;
};

struct Seed;   // a particle's own numbers (weather_field.cpp)

// The particles of a kind at a view: what all of them share worked out once (each axis's wrap, each fall group's
// height), so that each particle costs a few sums.
class Field {
public:
    Field(Kind kind, const FieldView& view);

    // The particles of the kind in the box.
    int Count() const;
    // The particles of bin `bin` (0 .. kBins - 1).
    BinRange Bin(int bin) const;
    // Where the particles of `bin` can be, each box grown by `margin` units on every side: up to eight boxes (a bin the
    // box's wall cuts in two on an axis); returns how many.
    int BinBounds(int bin, float margin, Bounds out[8]) const;
    // Particle `index`'s anchor, cheaper than Place: its position before the snow's drift, so within kMostDrift of its
    // place (the rain's is its place). False when it does not show.
    bool Anchor(int index, Vector3* point) const;
    // Particle `index` (0 .. Count - 1). Its alpha is 0 when it does not show.
    Particle Place(int index) const;

private:
    // Particle `index`'s seed and its anchor's offset from the centre; false when it does not show (the density leaves
    // it out, or it lies outside the box's circle by more than the drift takes it back).
    bool Locate(int index, const Seed** seed, float* shown, Vector3* offset) const;

    Kind kind = Kind::Rain;
    FieldView view;
    const Seed* seeds = nullptr;
    const int* binFirst = nullptr;   // kBins + 1
    int count = 0;
    // What a particle's own point plus these is, wrapped into the box, is its anchor's offset from the centre: each
    // horizontal axis's, and each fall group's height (-a side .. 0, -the height .. 0).
    float baseX = 0.0f, baseY = 0.0f;
    float baseZ[kFallGroups] = {};
};

// The view's carry moved on by `ticks` of `wind` and kept within one box on each axis.
Vector3 Carry(Vector3 carry, Vector3 wind, double ticks);

} // namespace weather
