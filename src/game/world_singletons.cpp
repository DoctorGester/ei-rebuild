// What the game's code reaches through the two worlds and the scene renderer, named once (game_singletons.h): the
// worlds' ticks, the client world's managers, the server world's mission script and sector map, the render device.

#include "game_singletons.h"

#include "effects.h"          // CParticleManager
#include "figure_manager.h"   // CFigureManager
#include "model.h"            // CSceneRenderer
#include "spell.h"            // CLightFactory
#include "texture.h"          // CPixelConverter
#include "world.h"
#include "world_data.h"

int CurrentWorldTick() { return g_serverWorld->tick_; }

int worlddata::FightState() { return g_clientWorld->partyDanger_; }

CLight* worlddata::SceneSun() { return g_sceneRenderer->sun_; }

CScript* TheMissionScript() { return g_serverWorld->MainScript(); }
