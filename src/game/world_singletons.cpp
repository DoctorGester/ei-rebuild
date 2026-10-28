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
int CurrentClientTick() { return g_clientWorld->tick_; }
float ClientTickFraction() { return g_clientWorld->tickFraction_; }

CTextManager* TheTextManager() { return g_clientWorld->texts_; }
CMapObjectManager* TheObjectManager() { return g_clientWorld->manager40_; }
CFigureManager* TheFigureManager() { return g_clientWorld->manager3C_; }
CTextureManager* TheTextureManager() { return g_clientWorld->textureManager_; }
CMaterialManager* TheMaterialManager() { return g_clientWorld->manager188_; }
CSoundManager* TheSoundManager() { return g_clientWorld->sounds_; }
CMusicSystem* TheMusicSystem() { return g_clientWorld->music_; }
CParticleManager* TheParticleManager() { return g_clientWorld->particleManager_; }
CLightFactory* TheLightFactory() { return g_clientWorld->manager190_; }
Terrain* TheTerrain() { return g_clientWorld->terrain_; }

int worlddata::FightState() { return g_clientWorld->partyDanger_; }

CTextureDevice* TheRenderDevice() { return TheSceneRenderer()->device_; }
CTransformPipeline* ScenePipeline() { return TheSceneRenderer()->pipeline_; }
CPixelConverter* PixelConverter() { return TheSceneRenderer()->graphicsProcessor_; }
CLight* worlddata::SceneSun() { return TheSceneRenderer()->sun_; }

CDiplomacy* GameDiplomacy() { return &g_serverWorld->diplomacy_; }
CScript* TheMissionScript() { return g_serverWorld->MainScript(); }
CSectorMap* TheSectorMap() { return &g_serverWorld->aiMap_.sectorMap; }
