// The constant virtuals and plain getters of the world classes. See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"

#include "effects.h"
#include "figure_manager.h"
#include "item.h"
#include "text_manager.h"
#include "lightning.h"
#include "spell.h"   // LightFactory
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

const char* WorldClient::ClassName() { return kWorldClientName; }
ClassId WorldClient::GetClassId() { return ClassId::WorldClient; }

const char* WorldServer::ClassName() { return kWorldServerName; }
ClassId WorldServer::GetClassId() { return ClassId::WorldServer; }

const char* PlatformSet::ClassName() { return kBridgeManagerName; }
ClassId PlatformSet::GetClassId() { return ClassId::PlatformSet; }

const char* ParticleManager::ClassName() { return kParticleManagerName; }
ClassId ParticleManager::GetClassId() { return ClassId::ParticleManager; }

const char* TextManager::ClassName() { return kTextManagerName; }
ClassId TextManager::GetClassId() { return ClassId::TextManager; }

const char* terrain::Terrain::ClassName() { return kTerrainName; }
ClassId terrain::Terrain::GetClassId() { return ClassId::Terrain; }

const char* LightningManager::ClassName() { return kLightningManagerName; }
ClassId LightningManager::GetClassId() { return ClassId::LightningManager; }

const char* ClientArrayManager::ClassName() { return kVisFogManagerName; }
ClassId ClientArrayManager::GetClassId() { return ClassId::ClientArrayManager; }

const char* CFigureManager::ClassName() { return kFigureManagerName; }
ClassId CFigureManager::GetClassId() { return ClassId::FigureManager; }
const char* CFigureManager::GetFolderName() { return kFiguresFolderName; }

const char* FigureClientSpecificClassName(EngineObject*) { return kFigureClientSpecificName; }


bool WorldServer::IsServer() { return true; }

const char* WorldClient::TexturePath() { return texturePath.c_str(); }
CMaterialManager* WorldClient::MaterialManager() { return materialManager; }
CTextureManager* WorldClient::TextureManager() { return textureManager; }
BaseManager* WorldClient::ClientLightManager() { return lightFactory; }
BaseManager* CWorld::FigureManager() { return figureManager; }
ParticleManager* WorldClient::ClientParticleManager() { return particleManager; }

void WorldClient::MoveObject(CMapObject* object, const float* position) {
    objectGrid.Move(object, position);
}
void WorldClient::MoveObject2(CMapObject* object, const float* position) {
    secondGrid.Move(object, position);
}
