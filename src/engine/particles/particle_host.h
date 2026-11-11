#pragma once

// What the particle core (particle_source.h, particle_data.h) asks of the world its sources are in: the ground and the
// platforms, the wind, how far the frame is between two ticks, the scorch marks, the camera's shakes and the carriers
// (the figures a source follows). The core includes no game header (docs/notes/file-layout.md, "The engine library");
// it asks the program's host (Use), of which there are two: the game's on the client world
// (game/effects/effects_particles.h) and particle_figure_host.h's, a world of its own for a program without the game's
// (the effect viewer's exe, effect_check's world-less case). See docs/notes/effects.md, "The host".

#include "lib/vecmath.h"

#include <cstdint>

class FigureInstance;
class ParticleSource;
enum class AttachPoint : std::uint8_t;   // particle_source.h

namespace particlehost {

// The world's wind, copied into each source at its tick.
struct Wind {
    Vector3 direction;
    float strength = 0.0f;
};

// A carrier's centre offset (0, 0, its bounds centre's z) and radius, its bounds measured again.
struct Measure {
    Vector3 offset;
    float radius = 0.0f;
};

// What a carrier is to its sources' effects (the carrier values, SourceValue::CarrierUnit ... particle_record.h).
struct CarrierState {
    bool unit = false;          // a unit (else its unit values are 0 and its feet at the source's origin)
    bool running = false;       // it runs
    bool dead = false;
    float boundsRadius = 0.0f;  // its bounds' radius as they are (not measured again)
    float unitRadius = 0.0f;    // a unit's own radius (by its size class)
    Vector3 leftFoot;           // a unit's feet: the points of its "ll3" and "rl3" parts
    Vector3 rightFoot;
};

class Host {
public:
    virtual ~Host() = default;

    // --- The world ---

    // The ground's (or the water's) height at (x, y), as a unit standing there has it.
    virtual float GroundHeight(float x, float y) = 0;
    // The top platform's plane at (x, y): `which` 0 its height, 1 to 3 its normal's x, y and z; 4 the height of what
    // stands there (the platform or else the ground).
    virtual float PlatformPlane(float x, float y, int which) = 0;
    virtual Wind CurrentWind() = 0;
    // How far the frame is between the last tick and the next (not clamped).
    virtual float TickFraction() = 0;
    // A scorch mark on the ground at (x, y): its two radii, its turn and its strength.
    virtual void AddScorchMark(float x, float y, float radiusX, float radiusY, float angle, float alpha) = 0;
    // A shake of the camera from `source` (Camera::Shake).
    virtual void ShakeCamera(const Vector3& source, float duration, float amplitude, float decay) = 0;

    // --- The carriers (never null here: the core answers for a free source) ---

    // `carrier`'s bounds measured and `source` added to its sources, a reference held (ParticleSource::Attach).
    virtual void Follow(FigureInstance& carrier, ParticleSource* source) = 0;
    // `source` dropped from `carrier`'s sources: it lets go of the carrier (Attach(null)), then its reference goes.
    virtual void LetGo(FigureInstance& carrier, ParticleSource* source) = 0;
    // Whether `carrier` was taken out of the world (the source then lets go at its tick).
    virtual bool Removed(const FigureInstance& carrier) = 0;
    // Whether `carrier`'s sources are not drawn (a unit out of the party).
    virtual bool Hidden(FigureInstance& carrier) = 0;
    // Where a source on `part` (ParticleSourceFlag::OnPart) sits on `carrier`: on a unit the part `part` names (or the
    // unit's effect point), on another object the top of its bounds.
    virtual Vector3 PartPoint(FigureInstance& carrier, AttachPoint part) = 0;
    virtual Measure MeasureCarrier(FigureInstance& carrier) = 0;
    virtual CarrierState DescribeCarrier(FigureInstance& carrier) = 0;
};

// The program's host: what every source asks from now on. Set before the first source is made.
void Use(Host& host);
Host& Current();

} // namespace particlehost
