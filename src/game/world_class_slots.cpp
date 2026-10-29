// The constant virtuals and plain getters of the world classes. See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"

#include "effects.h"
#include "figure_manager.h"
#include "item.h"
#include "text_manager.h"
#include "lightning.h"
#include "spell.h"   // CLightFactory
#include "terrain.h"
#include "world.h"

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
constexpr const char* kFigureClientSpecificName = "CFigureClientSpecific";
constexpr const char* kFiguresFolderName = "figures";

} // namespace

const char* CWorldClient::ClassName() { return kWorldClientName; }
ClassId CWorldClient::GetClassId() { return ClassId::WorldClient; }

const char* CWorldServer::ClassName() { return kWorldServerName; }
ClassId CWorldServer::GetClassId() { return ClassId::WorldServer; }

const char* PlatformSet::ClassName() { return kBridgeManagerName; }
ClassId PlatformSet::GetClassId() { return ClassId::PlatformSet; }

const char* CParticleManager::ClassName() { return kParticleManagerName; }
ClassId CParticleManager::GetClassId() { return ClassId::ParticleManager; }

const char* CTextManager::ClassName() { return kTextManagerName; }
ClassId CTextManager::GetClassId() { return ClassId::TextManager; }

const char* Terrain::ClassName() { return kTerrainName; }
ClassId Terrain::GetClassId() { return ClassId::Terrain; }

const char* CLightningManager::ClassName() { return kLightningManagerName; }
ClassId CLightningManager::GetClassId() { return ClassId::LightningManager; }

const char* ClientArrayManager::ClassName() { return kVisFogManagerName; }
ClassId ClientArrayManager::GetClassId() { return ClassId::ClientArrayManager; }

const char* CFigureManager::ClassName() { return kFigureManagerName; }
ClassId CFigureManager::GetClassId() { return ClassId::FigureManager; }
const char* CFigureManager::GetFolderName() { return kFiguresFolderName; }

const char* FigureClientSpecificClassName(CEngineObject*) { return kFigureClientSpecificName; }


bool CWorldServer::IsServer() { return true; }

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
