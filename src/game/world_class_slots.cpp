// The constant virtuals and plain getters of the world classes. See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"
#include "game_strings.h"

#include "world.h"
#include "figure_manager.h"
#include "spell.h"   // CLightFactory
#include "effects.h"
#include "item.h"
#include "item_db_text.h"
#include "lightning.h"
#include "terrain.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

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

constexpr int kWorldClientId = 7;
constexpr int kWorldServerId = 8;
constexpr int kBridgeManagerId = 0x34;
constexpr int kParticleManagerId = 0x2C;
constexpr int kTextManagerId = 0x2A;
constexpr int kTerrainId = 0xB;
constexpr int kLightningManagerId = 0x35;
constexpr int kVisFogManagerId = 0x60;
constexpr int kFigureManagerId = 0x28;
constexpr int kI3DFigureId = 0x46;
constexpr int kFigureClientSpecificId = 0x36;
constexpr int kUiObjectId = 0x45;

} // namespace

const char* CWorldClient::GetClassName() { return kWorldClientName; }
int CWorldClient::GetClassId() { return kWorldClientId; }

const char* CWorldServer::GetClassName() { return kWorldServerName; }
int CWorldServer::GetClassId() { return kWorldServerId; }

const char* PlatformSet::GetClassName() { return kBridgeManagerName; }
int PlatformSet::GetClassId() { return kBridgeManagerId; }

const char* CParticleManager::GetClassName() { return kParticleManagerName; }
int CParticleManager::GetClassId() { return kParticleManagerId; }

const char* CTextManager::GetClassName() { return kTextManagerName; }
int CTextManager::GetClassId() { return kTextManagerId; }

const char* Terrain::GetClassName() { return kTerrainName; }
int Terrain::GetClassId() { return kTerrainId; }

const char* CLightningManager::GetClassName() { return kLightningManagerName; }
int CLightningManager::GetClassId() { return kLightningManagerId; }

const char* ClientArrayManager::GetClassName() { return kVisFogManagerName; }
int ClientArrayManager::GetClassId() { return kVisFogManagerId; }

const char* CFigureManager::GetClassName() { return kFigureManagerName; }
int CFigureManager::GetClassId() { return kFigureManagerId; }
const char* CFigureManager::GetFolderName() { return kFiguresFolderName; }

const char* I3DFigureClassName(CEngineObject*) { return kI3DFigureName; }
int I3DFigureClassId(CEngineObject*) { return kI3DFigureId; }

const char* FigureClientSpecificClassName(CEngineObject*) { return kFigureClientSpecificName; }
int FigureClientSpecificClassId(CEngineObject*) { return kFigureClientSpecificId; }

int UiObjectCreate(CEngineObject*) { return 1; }
const char* UiObjectClassName(CEngineObject*) { return kUiObjectName; }
int UiObjectClassId(CEngineObject*) { return kUiObjectId; }

int CWorldServer::IsServer() { return 1; }

const char* CWorldClient::TexturePath() { return texturePath_.c_str(); }
CMaterialManager* CWorldClient::MaterialManager() { return manager188_; }
CTextureManager* CWorldClient::TextureManager() { return textureManager_; }
CBaseManager* CWorldClient::ClientLightManager() { return manager190_; }
CBaseManager* CWorld::FigureManager() { return manager3C_; }
CParticleManager* CWorldClient::ClientParticleManager() { return particleManager_; }
CEngineObject* CWorldClient::ClientManager194() { return manager194_; }
CEngineObject* CWorldClient::ClientManager198() { return manager198_; }

void CWorldClient::MoveObject(CMapObject* object, const float* position) {
    objectGrid_.Move(object, position);
}
void CWorldClient::MoveObject2(CMapObject* object, const float* position) {
    secondGrid_.Move2(object, position);
}
