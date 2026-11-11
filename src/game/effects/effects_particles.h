#pragma once

// The game's answers to the particle core (engine/particles/particle_host.h): the client world's ground, platforms,
// wind, tick fraction, scorch marks and camera, and the map objects and units the sources follow.

#include "engine/particles/particle_host.h"

// The client world's host, used (particlehost::Use) where a particle manager for the client world is made.
particlehost::Host& WorldParticleHost();
