// The constant virtuals and plain getters of the world classes. See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"

#include "world.h"
#include "figure_manager.h"
#include "spell.h"   // CLightFactory
#include "effects.h"
#include "item.h"
#include "item_db_text.h"
#include "lightning.h"
#include "terrain.h"


namespace {

// The class and folder names in the exe's .data (the callers compare and print them).
constexpr const char* kWorldClientName = "CWorldClient";
constexpr const char* kWorldServerName = "CWorldServer";
constexpr const char* kBridgeManagerName = "CBridgeManager";
constexpr const char* kParticleManagerName = "CParticleManager";
constexpr const char* kTextManagerName = "CTextManager";
constexpr const char* kTerrainName = "CTerrain";
constexpr const char* kLightningManagerName = "CLightningManager";
constexpr const char* kVisFogManagerName = "CVisFogManager";
constexpr const char* kFigureManagerName = "CFigureManager";
constexpr const char* kI3DFigureName = "CI3DFigure";
constexpr const char* kFigureClientSpecificName = "CFigureClientSpecific";
constexpr const char* kUiObjectName = "Sorry, no other way.";
constexpr const char* kFiguresFolderName = "figures";

} // namespace

const char* CWorldClient::ClassName() { return kWorldClientName; }
ClassId CWorldClient::GetClassId() { return kClassWorldClient; }

const char* CWorldServer::ClassName() { return kWorldServerName; }
ClassId CWorldServer::GetClassId() { return kClassWorldServer; }

const char* PlatformSet::ClassName() { return kBridgeManagerName; }
ClassId PlatformSet::GetClassId() { return kClassPlatformSet; }

const char* CParticleManager::ClassName() { return kParticleManagerName; }
ClassId CParticleManager::GetClassId() { return kClassParticleManager; }

const char* CTextManager::ClassName() { return kTextManagerName; }
ClassId CTextManager::GetClassId() { return kClassTextManager; }

const char* Terrain::ClassName() { return kTerrainName; }
ClassId Terrain::GetClassId() { return kClassTerrain; }

const char* CLightningManager::ClassName() { return kLightningManagerName; }
ClassId CLightningManager::GetClassId() { return kClassLightningManager; }

const char* ClientArrayManager::ClassName() { return kVisFogManagerName; }
ClassId ClientArrayManager::GetClassId() { return kClassClientArrayManager; }

const char* CFigureManager::ClassName() { return kFigureManagerName; }
ClassId CFigureManager::GetClassId() { return kClassFigureManager; }
const char* CFigureManager::GetFolderName() { return kFiguresFolderName; }

const char* I3DFigureClassName(CEngineObject*) { return kI3DFigureName; }

const char* FigureClientSpecificClassName(CEngineObject*) { return kFigureClientSpecificName; }

int UiObjectCreate(CEngineObject*) { return 1; }
const char* UiObjectClassName(CEngineObject*) { return kUiObjectName; }

int CWorldServer::IsServer() { return 1; }

const char* CWorldClient::TexturePath() { return texturePath_.c_str(); }
CMaterialManager* CWorldClient::MaterialManager() { return materialManager_; }
CTextureManager* CWorldClient::TextureManager() { return textureManager_; }
CBaseManager* CWorldClient::ClientLightManager() { return lightFactory_; }
CBaseManager* CWorld::FigureManager() { return figureManager_; }
CParticleManager* CWorldClient::ClientParticleManager() { return particleManager_; }

void CWorldClient::MoveObject(CMapObject* object, const float* position) {
    objectGrid_.Move(object, position);
}
void CWorldClient::MoveObject2(CMapObject* object, const float* position) {
    secondGrid_.Move(object, position);
}
