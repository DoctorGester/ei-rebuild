// What the game's code reaches through the two worlds and the scene renderer, named once (game_singletons.h): the
// client world's managers, the server world's mission script and sector map, the render device.

#include "game_singletons.h"

#include "effects.h"          // CParticleManager
#include "figure_manager.h"   // CFigureManager
#include "model.h"            // CSceneRenderer
#include "spell.h"            // CLightFactory
#include "world.h"
#include "world_data.h"

CScript* TheMissionScript() { return g_serverWorld.MainScript(); }
