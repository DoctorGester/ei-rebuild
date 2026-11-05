#pragma once

// The rain's and the snow's particles as a field around the view: where each one is at a moment and how much of it
// shows. Client-only and without state of its own: every particle's place follows from its number, the world's time
// and the wind carried so far, so a frame shows the same field as the last one moved on by the time between them.
// weather_particles.cpp draws it; docs/notes/renderer.md, section 12, "Weather".
//
// The particles fill a box of kBoxSide x kBoxSide x kBoxHeight units that repeats over the world: a particle's place
// is its own point in the box, carried by the wind and its fall, taken to the copy of the box around the field's
// centre. A particle leaves the box on one side as it comes back in on the other, so its alpha fades to nothing at
// the box's walls (and near the eye): it never jumps or vanishes while it is seen.

#include "lib/vecmath.h"

namespace weather {

enum class Kind { Rain, Snow };

// The units the box spans: the horizontal sides and the height.
constexpr float kBoxSide = 80.0f;
constexpr float kBoxHeight = 40.0f;

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
};

// The particles of a kind in the box.
int Count(Kind kind);

// Particle `index` (0 .. Count - 1) at the view. Its alpha is 0 when it does not show.
Particle Place(Kind kind, int index, const FieldView& view);

// The view's carry moved on by `ticks` of `wind` and kept within one box on each axis.
Vector3 Carry(Vector3 carry, Vector3 wind, double ticks);

} // namespace weather
