#pragma once

// What the particle core (particle_source.h, particle_data.h) asks of the world its sources are in: the ground and the
// platforms, the wind, how far the frame is between two ticks, the scorch marks, and the carriers (the map objects a
// source follows). The game answers on the client world (effects/effects_particles.cpp); the core includes no game
// header (docs/notes/file-layout.md, "The engine library"). Free functions the game defines for now; a program without
// the world (the effect viewer's own exe) is to answer them too, through an interface (docs/notes/effects.md).

#include "lib/vecmath.h"

#include <vector>

class MapObject;
class ParticleSource;

namespace particlehost {

// The ground's (or the water's) height at (x, y), as a unit standing there has it.
float GroundHeight(float x, float y);
// The top platform's plane at (x, y): `which` 0 its height, 1 to 3 its normal's x, y and z; 4 the height of what
// stands there (the platform or else the ground).
float PlatformPlane(float x, float y, int which);
// The world's wind, copied into each source at its tick.
struct Wind {
    Vector3 direction;
    float strength = 0.0f;
};
Wind CurrentWind();
// How far the frame is between the last tick and the next (not clamped).
float TickFraction();
// A scorch mark on the ground at (x, y): its two radii, its turn and its strength.
void AddScorchMark(float x, float y, float radiusX, float radiusY, float angle, float alpha);

// `carrier`'s bounds measured and `source` added to its sources (ParticleSource::Attach).
void Follow(MapObject* carrier, ParticleSource* source);
// `source` dropped from `carrier`'s sources.
void LetGo(MapObject* carrier, ParticleSource* source);
// Whether `carrier` was taken out of the world (the source then lets go).
bool Removed(const MapObject* carrier);
// Whether `carrier` is a unit out of the party: its sources are not drawn. False for null.
bool Hidden(MapObject* carrier);
// Where `source` sits on `carrier`: with ParticleSourceFlag::OnPart on a unit the part its attachPoint names (or the
// unit's effect point), on another object the top of its bounds; without OnPart the carrier's position. (0, 0, 0)
// for null.
Vector3 CarrierPoint(const ParticleSource& source, MapObject* carrier);
// Offset (0, 0, the bounds centre's z) and radius of `carrier` (its bounds measured again); (0, 0, 0) and 0.1 for
// null.
struct Measure {
    Vector3 offset;
    float radius = 0.0f;
};
Measure MeasureCarrier(MapObject* carrier);
// The carrier's values (SourceValue::CarrierUnit ... CarrierRightFootZ, particle_record.h) of `source`'s carrier into
// `values`.
void WriteCarrier(const ParticleSource& source, std::vector<float>& values);

} // namespace particlehost
