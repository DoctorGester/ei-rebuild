#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CParticleManager;
class CSpell;
class CSpellConstructor;
class CSpellManager;
struct CSpellEffect;
struct CMusicSystem;
struct CSoundManager;
struct DamageInfo;
struct Vector3;

// 006CA5D0: the light manager (*007AF738) adds a light (RET 4, no result).
// 00511FB0: the light factory's (*007AF748) list of lit ids (+0xD4): removes every entry equal to *id (RET 4).


class CRainSnow;

