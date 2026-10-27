// The constant virtuals and plain getters of the world classes. See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"
#include "game_strings.h"

#include "world.h"
#include "figure_manager.h"
#include "effects.h"
#include "item.h"
#include "item_db_text.h"
#include "lightning.h"
#include "terrain.h"

#include "ei/hook.h"

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
EI_REWRITTEN(0x005619E0, CWorldClient::GetClassName);
EI_REWRITTEN(0x005619F0, CWorldClient::GetClassId);

const char* CWorldServer::GetClassName() { return kWorldServerName; }
int CWorldServer::GetClassId() { return kWorldServerId; }
EI_REWRITTEN(0x0058A410, CWorldServer::GetClassName);
EI_REWRITTEN(0x0058A420, CWorldServer::GetClassId);

const char* PlatformSet::GetClassName() { return kBridgeManagerName; }
int PlatformSet::GetClassId() { return kBridgeManagerId; }
EI_REWRITTEN(0x00561870, PlatformSet::GetClassName);
EI_REWRITTEN(0x00561880, PlatformSet::GetClassId);

const char* CParticleManager::GetClassName() { return kParticleManagerName; }
int CParticleManager::GetClassId() { return kParticleManagerId; }
EI_REWRITTEN(0x00562780, CParticleManager::GetClassName);
EI_REWRITTEN(0x00562790, CParticleManager::GetClassId);

const char* CTextManager::GetClassName() { return kTextManagerName; }
int CTextManager::GetClassId() { return kTextManagerId; }
EI_REWRITTEN(0x00562970, CTextManager::GetClassName);
EI_REWRITTEN(0x00562980, CTextManager::GetClassId);

const char* Terrain::GetClassName() { return kTerrainName; }
int Terrain::GetClassId() { return kTerrainId; }
EI_REWRITTEN(0x00562B40, Terrain::GetClassName);
EI_REWRITTEN(0x00562B50, Terrain::GetClassId);

const char* CLightningManager::GetClassName() { return kLightningManagerName; }
int CLightningManager::GetClassId() { return kLightningManagerId; }
EI_REWRITTEN(0x00562D80, CLightningManager::GetClassName);
EI_REWRITTEN(0x00562D90, CLightningManager::GetClassId);

const char* ClientArrayManager::GetClassName() { return kVisFogManagerName; }
int ClientArrayManager::GetClassId() { return kVisFogManagerId; }
EI_REWRITTEN(0x00562DB0, ClientArrayManager::GetClassName);
EI_REWRITTEN(0x00562DC0, ClientArrayManager::GetClassId);

const char* CFigureManager::GetClassName() { return kFigureManagerName; }
int CFigureManager::GetClassId() { return kFigureManagerId; }
const char* CFigureManager::GetFolderName() { return kFiguresFolderName; }

const char* __fastcall I3DFigureClassName(CEngineObject*) { return kI3DFigureName; }
int __fastcall I3DFigureClassId(CEngineObject*) { return kI3DFigureId; }
EI_REWRITTEN(0x005AACC0, I3DFigureClassName);
EI_REWRITTEN(0x005AACD0, I3DFigureClassId);

const char* __fastcall FigureClientSpecificClassName(CEngineObject*) { return kFigureClientSpecificName; }
int __fastcall FigureClientSpecificClassId(CEngineObject*) { return kFigureClientSpecificId; }
EI_REWRITTEN(0x005A7040, FigureClientSpecificClassName);
EI_REWRITTEN(0x005A7050, FigureClientSpecificClassId);

int __fastcall UiObjectCreate(CEngineObject*) { return 1; }
const char* __fastcall UiObjectClassName(CEngineObject*) { return kUiObjectName; }
int __fastcall UiObjectClassId(CEngineObject*) { return kUiObjectId; }
EI_REWRITTEN(0x00579560, UiObjectCreate);
EI_REWRITTEN(0x00579580, UiObjectClassName);
EI_REWRITTEN(0x00579590, UiObjectClassId);

EI_REWRITTEN(0x0055F3B0, CItemManager::GetFolderName);

int CWorldServer::IsServer() { return 1; }
EI_REWRITTEN(0x0058A380, CWorldServer::IsServer);

const char* CWorldClient::TexturePath() { return texturePath_; }
CMaterialManager* CWorldClient::MaterialManager() { return manager188_; }
CTextureManager* CWorldClient::TextureManager() { return textureManager_; }
CBaseManager* CWorldClient::ClientLightManager() { return manager190_; }
CParticleManager* CWorldClient::ClientParticleManager() { return particleManager_; }
CEngineObject* CWorldClient::ClientManager194() { return manager194_; }
CEngineObject* CWorldClient::ClientManager198() { return manager198_; }
EI_REWRITTEN(0x00561890, CWorldClient::TexturePath);
EI_REWRITTEN(0x00561980, CWorldClient::MaterialManager);
EI_REWRITTEN(0x00561990, CWorldClient::TextureManager);
EI_REWRITTEN(0x005619A0, CWorldClient::ClientLightManager);
EI_REWRITTEN(0x005619B0, CWorldClient::ClientParticleManager);
EI_REWRITTEN(0x005619C0, CWorldClient::ClientManager194);
EI_REWRITTEN(0x005619D0, CWorldClient::ClientManager198);

void CWorldClient::MoveObject(CEngineObject* object, const float* position) {
    objectGrid_.Move(object, position);
}
void CWorldClient::MoveObject2(CEngineObject* object, const float* position) {
    secondGrid_.Move2(object, position);
}
EI_REWRITTEN(0x005618A0, CWorldClient::MoveObject);
EI_REWRITTEN(0x005618C0, CWorldClient::MoveObject2);

EI_REWRITTEN(0x00562D70, CBaseManager::Init);   // the lightning manager's Init
EI_REWRITTEN(0x00562DA0, CBaseManager::Init);   // the vis-fog manager's Init (ClientArrayManager)
