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

int CurrentWorldTick() { return TheServerWorld()->tick_; }
int CurrentClientTick() { return TheClientWorld()->tick_; }
float ClientTickFraction() { return TheClientWorld()->tickFraction_; }

CTextManager* TheTextManager() { return TheClientWorld()->texts_; }
CMapObjectManager* TheObjectManager() { return TheClientWorld()->manager40_; }
CFigureManager* TheFigureManager() { return TheClientWorld()->manager3C_; }
CTextureManager* TheTextureManager() { return TheClientWorld()->textureManager_; }
CMaterialManager* TheMaterialManager() { return TheClientWorld()->manager188_; }
CSoundManager* TheSoundManager() { return TheClientWorld()->sounds_; }
CMusicSystem* TheMusicSystem() { return TheClientWorld()->music_; }
CParticleManager* TheParticleManager() { return TheClientWorld()->particleManager_; }
CLightFactory* TheLightFactory() { return TheClientWorld()->manager190_; }
Terrain* TheTerrain() { return TheClientWorld()->terrain_; }

int worlddata::FightState() { return TheClientWorld()->partyDanger_; }

CTextureDevice* TheRenderDevice() { return TheSceneRenderer()->device_; }
CTransformPipeline* ScenePipeline() { return TheSceneRenderer()->pipeline_; }
CPixelConverter* PixelConverter() { return TheSceneRenderer()->graphicsProcessor_; }
CLight* worlddata::SceneSun() { return TheSceneRenderer()->sun_; }

CDiplomacy* GameDiplomacy() { return &TheServerWorld()->diplomacy_; }
CScript* TheMissionScript() { return TheServerWorld()->MainScript(); }
CSectorMap* TheSectorMap() { return TheServerWorld()->Sectors(); }
