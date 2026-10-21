// The constant slots and plain getters of the world classes (A-W14). See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"
#include "game_strings.h"

#include "world.h"
#include "figure_manager.h"

#include "ei/hook.h"

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
constexpr const char* kItemsFolderName = "items";
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

const char* __fastcall WorldClientClassName(CWorldClient*) { return kWorldClientName; }
int __fastcall WorldClientClassId(CWorldClient*) { return kWorldClientId; }
EI_REPLACE(0x005619E0, WorldClientClassName);
EI_REPLACE(0x005619F0, WorldClientClassId);

const char* __fastcall WorldServerClassName(CWorldServer*) { return kWorldServerName; }
int __fastcall WorldServerClassId(CWorldServer*) { return kWorldServerId; }
EI_REPLACE(0x0058A410, WorldServerClassName);
EI_REPLACE(0x0058A420, WorldServerClassId);

const char* __fastcall BridgeManagerClassName(PlatformSet*) { return kBridgeManagerName; }
int __fastcall BridgeManagerClassId(PlatformSet*) { return kBridgeManagerId; }
EI_REPLACE(0x00561870, BridgeManagerClassName);
EI_REPLACE(0x00561880, BridgeManagerClassId);

const char* __fastcall ParticleManagerClassName(CParticleManager*) { return kParticleManagerName; }
int __fastcall ParticleManagerClassId(CParticleManager*) { return kParticleManagerId; }
EI_REPLACE(0x00562780, ParticleManagerClassName);
EI_REPLACE(0x00562790, ParticleManagerClassId);

const char* __fastcall TextManagerClassName(ClientTexts*) { return kTextManagerName; }
int __fastcall TextManagerClassId(ClientTexts*) { return kTextManagerId; }
EI_REPLACE(0x00562970, TextManagerClassName);
EI_REPLACE(0x00562980, TextManagerClassId);

const char* __fastcall TerrainClassName(Terrain*) { return kTerrainName; }
int __fastcall TerrainClassId(Terrain*) { return kTerrainId; }
EI_REPLACE(0x00562B40, TerrainClassName);
EI_REPLACE(0x00562B50, TerrainClassId);

const char* __fastcall LightningManagerClassName(ClientListManager*) { return kLightningManagerName; }
int __fastcall LightningManagerClassId(ClientListManager*) { return kLightningManagerId; }
EI_REPLACE(0x00562D80, LightningManagerClassName);
EI_REPLACE(0x00562D90, LightningManagerClassId);

const char* __fastcall VisFogManagerClassName(ClientArrayManager*) { return kVisFogManagerName; }
int __fastcall VisFogManagerClassId(ClientArrayManager*) { return kVisFogManagerId; }
EI_REPLACE(0x00562DB0, VisFogManagerClassName);
EI_REPLACE(0x00562DC0, VisFogManagerClassId);

const char* CFigureManager::GetClassName() { return kFigureManagerName; }
int CFigureManager::GetClassId() { return kFigureManagerId; }
const char* CFigureManager::GetFolderName() { return kFiguresFolderName; }

const char* __fastcall I3DFigureClassName(CEngineObject*) { return kI3DFigureName; }
int __fastcall I3DFigureClassId(CEngineObject*) { return kI3DFigureId; }
EI_REPLACE(0x005AACC0, I3DFigureClassName);
EI_REPLACE(0x005AACD0, I3DFigureClassId);

const char* __fastcall FigureClientSpecificClassName(CEngineObject*) { return kFigureClientSpecificName; }
int __fastcall FigureClientSpecificClassId(CEngineObject*) { return kFigureClientSpecificId; }
EI_REPLACE(0x005A7040, FigureClientSpecificClassName);
EI_REPLACE(0x005A7050, FigureClientSpecificClassId);

int __fastcall UiObjectCreate(CEngineObject*) { return 1; }
const char* __fastcall UiObjectClassName(CEngineObject*) { return kUiObjectName; }
int __fastcall UiObjectClassId(CEngineObject*) { return kUiObjectId; }
EI_REPLACE(0x00579560, UiObjectCreate);
EI_REPLACE(0x00579580, UiObjectClassName);
EI_REPLACE(0x00579590, UiObjectClassId);

const char* __fastcall ItemManagerResourceName(CItemManager*) { return kItemsFolderName; }
EI_REPLACE(0x0055F3B0, ItemManagerResourceName);

int __fastcall WorldBaseRunTick(CWorld*) { return 1; }
int __fastcall WorldServerIsServer(CWorldServer*) { return 1; }
int __fastcall PartyBaseCreate(CEngineObject*) { return 1; }
EI_REPLACE(0x0055FAD0, WorldBaseRunTick);
EI_REPLACE(0x0058A380, WorldServerIsServer);
EI_REPLACE(0x0055E930, PartyBaseCreate);

const char* __fastcall WorldClientTexturePath(CWorldClient* self) { return self->texturePath_; }
CBaseManager* __fastcall WorldClientMaterialManager(CWorldClient* self) { return self->manager188_; }
CTextureManager* __fastcall WorldClientTextureManager(CWorldClient* self) { return self->textureManager_; }
CBaseManager* __fastcall WorldClientLightManager(CWorldClient* self) { return self->manager190_; }
CParticleManager* __fastcall WorldClientParticleManager(CWorldClient* self) { return self->particleManager_; }
CEngineObject* __fastcall WorldClientManager194(CWorldClient* self) { return self->manager194_; }
CEngineObject* __fastcall WorldClientManager198(CWorldClient* self) { return self->manager198_; }
EI_REPLACE(0x00561890, WorldClientTexturePath);
EI_REPLACE(0x00561980, WorldClientMaterialManager);
EI_REPLACE(0x00561990, WorldClientTextureManager);
EI_REPLACE(0x005619A0, WorldClientLightManager);
EI_REPLACE(0x005619B0, WorldClientParticleManager);
EI_REPLACE(0x005619C0, WorldClientManager194);
EI_REPLACE(0x005619D0, WorldClientManager198);

void CWorldClient::MoveObjectImpl(CEngineObject* object, const float* position) {
    objectGrid_.Move(object, position);
}
void CWorldClient::MoveObject2Impl(CEngineObject* object, const float* position) {
    secondGrid_.Move2(object, position);
}
EI_REPLACE_METHOD(0x005618A0, CWorldClient::MoveObjectImpl);
EI_REPLACE_METHOD(0x005618C0, CWorldClient::MoveObject2Impl);

EI_REPLACE_METHOD(0x00562D70, CBaseManager::InitImpl);   // the lightning manager's Init (ClientListManager)
EI_REPLACE_METHOD(0x00562DA0, CBaseManager::InitImpl);   // the vis-fog manager's Init (ClientArrayManager)
