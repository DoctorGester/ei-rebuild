// What the game's code reaches through the two worlds and the scene renderer, named once (game_singletons.h): the
// client world's managers, the server world's mission script and sector map, the render device.

#include "game_singletons.h"

#include "effects.h"          // ParticleManager
#include "figure_manager.h"   // CFigureManager
#include "model.h"            // SceneRenderer
#include "spell.h"            // LightFactory
#include "world.h"
#include "world_data.h"

Script* TheMissionScript() { return g_serverWorld.MainScript(); }
