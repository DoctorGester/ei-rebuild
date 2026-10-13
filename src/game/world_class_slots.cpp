// The constant slots and plain getters of the world classes (A-W14). See world_class_slots.h and
// docs/notes/world.md.
#include "world_class_slots.h"

#include "world.h"

#include "ei/hook.h"

namespace {

// The class and folder names in the exe's .data (the callers compare and print them).
constexpr std::uintptr_t kWorldClientName = std::uintptr_t(0x0078FF8C);            // "CWorldClient"
constexpr std::uintptr_t kWorldServerName = std::uintptr_t(0x00790814);            // "CWorldServer"
constexpr std::uintptr_t kBridgeManagerName = std::uintptr_t(0x0078FF7C);          // "CBridgeManager"
constexpr std::uintptr_t kParticleManagerName = std::uintptr_t(0x0078FFF0);        // "CParticleManager"
constexpr std::uintptr_t kTextManagerName = std::uintptr_t(0x00790004);            // "CTextManager"
constexpr std::uintptr_t kTerrainName = std::uintptr_t(0x00790014);                // "CTerrain"
constexpr std::uintptr_t kLightningManagerName = std::uintptr_t(0x00790020);       // "CLightningManager"
constexpr std::uintptr_t kVisFogManagerName = std::uintptr_t(0x00790034);          // "CVisFogManager"
constexpr std::uintptr_t kFigureManagerName = std::uintptr_t(0x00790C50);          // "CFigureManager"
constexpr std::uintptr_t kI3DFigureName = std::uintptr_t(0x00790C60);              // "CI3DFigure"
constexpr std::uintptr_t kFigureClientSpecificName = std::uintptr_t(0x00790C14);   // "CFigureClientSpecific"
constexpr std::uintptr_t kUiObjectName = std::uintptr_t(0x007904F8);               // "Sorry, no other way."
constexpr std::uintptr_t kItemsFolderName = std::uintptr_t(0x0078FF08);            // "items"
constexpr std::uintptr_t kFiguresFolderName = std::uintptr_t(0x00790C48);          // "figures"

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

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

} // namespace

const char* __fastcall WorldClientClassName(CWorldClient*) { return Text(kWorldClientName); }
int __fastcall WorldClientClassId(CWorldClient*) { return kWorldClientId; }
EI_REPLACE(0x005619E0, WorldClientClassName);
EI_REPLACE(0x005619F0, WorldClientClassId);

const char* __fastcall WorldServerClassName(CWorldServer*) { return Text(kWorldServerName); }
int __fastcall WorldServerClassId(CWorldServer*) { return kWorldServerId; }
EI_REPLACE(0x0058A410, WorldServerClassName);
EI_REPLACE(0x0058A420, WorldServerClassId);

const char* __fastcall BridgeManagerClassName(PlatformSet*) { return Text(kBridgeManagerName); }
int __fastcall BridgeManagerClassId(PlatformSet*) { return kBridgeManagerId; }
EI_REPLACE(0x00561870, BridgeManagerClassName);
EI_REPLACE(0x00561880, BridgeManagerClassId);

const char* __fastcall ParticleManagerClassName(CParticleManager*) { return Text(kParticleManagerName); }
int __fastcall ParticleManagerClassId(CParticleManager*) { return kParticleManagerId; }
EI_REPLACE(0x00562780, ParticleManagerClassName);
EI_REPLACE(0x00562790, ParticleManagerClassId);

const char* __fastcall TextManagerClassName(ClientTexts*) { return Text(kTextManagerName); }
int __fastcall TextManagerClassId(ClientTexts*) { return kTextManagerId; }
EI_REPLACE(0x00562970, TextManagerClassName);
EI_REPLACE(0x00562980, TextManagerClassId);

const char* __fastcall TerrainClassName(Terrain*) { return Text(kTerrainName); }
int __fastcall TerrainClassId(Terrain*) { return kTerrainId; }
EI_REPLACE(0x00562B40, TerrainClassName);
EI_REPLACE(0x00562B50, TerrainClassId);

const char* __fastcall LightningManagerClassName(ClientListManager*) { return Text(kLightningManagerName); }
int __fastcall LightningManagerClassId(ClientListManager*) { return kLightningManagerId; }
EI_REPLACE(0x00562D80, LightningManagerClassName);
EI_REPLACE(0x00562D90, LightningManagerClassId);

const char* __fastcall VisFogManagerClassName(ClientArrayManager*) { return Text(kVisFogManagerName); }
int __fastcall VisFogManagerClassId(ClientArrayManager*) { return kVisFogManagerId; }
EI_REPLACE(0x00562DB0, VisFogManagerClassName);
EI_REPLACE(0x00562DC0, VisFogManagerClassId);

const char* __fastcall FigureManagerClassName(CFigureManager*) { return Text(kFigureManagerName); }
int __fastcall FigureManagerClassId(CFigureManager*) { return kFigureManagerId; }
EI_REPLACE(0x005AA180, FigureManagerClassName);
EI_REPLACE(0x005AA190, FigureManagerClassId);

const char* __fastcall I3DFigureClassName(CEngineObject*) { return Text(kI3DFigureName); }
int __fastcall I3DFigureClassId(CEngineObject*) { return kI3DFigureId; }
EI_REPLACE(0x005AACC0, I3DFigureClassName);
EI_REPLACE(0x005AACD0, I3DFigureClassId);

const char* __fastcall FigureClientSpecificClassName(CEngineObject*) { return Text(kFigureClientSpecificName); }
int __fastcall FigureClientSpecificClassId(CEngineObject*) { return kFigureClientSpecificId; }
EI_REPLACE(0x005A7040, FigureClientSpecificClassName);
EI_REPLACE(0x005A7050, FigureClientSpecificClassId);

int __fastcall UiObjectCreate(CEngineObject*) { return 1; }
const char* __fastcall UiObjectClassName(CEngineObject*) { return Text(kUiObjectName); }
int __fastcall UiObjectClassId(CEngineObject*) { return kUiObjectId; }
EI_REPLACE(0x00579560, UiObjectCreate);
EI_REPLACE(0x00579580, UiObjectClassName);
EI_REPLACE(0x00579590, UiObjectClassId);

const char* __fastcall ItemManagerResourceName(CItemManager*) { return Text(kItemsFolderName); }
const char* __fastcall FigureManagerResourceName(CFigureManager*) { return Text(kFiguresFolderName); }
EI_REPLACE(0x0055F3B0, ItemManagerResourceName);
EI_REPLACE(0x005AA170, FigureManagerResourceName);

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

void __fastcall WorldClientMoveObject(CWorldClient* self, void*, CEngineObject* object, const float* position) {
    self->objectGrid_.Move(object, position);
}
void __fastcall WorldClientMoveObject2(CWorldClient* self, void*, CEngineObject* object, const float* position) {
    self->secondGrid_.Move2(object, position);
}
EI_REPLACE(0x005618A0, WorldClientMoveObject);
EI_REPLACE(0x005618C0, WorldClientMoveObject2);

int __fastcall LightningManagerInit(ClientListManager* self, void*, void* context) {
    return self->CBaseManager::Init(context);
}
int __fastcall VisFogManagerInit(ClientArrayManager* self, void*, void* context) {
    return self->CBaseManager::Init(context);
}
EI_REPLACE(0x00562D70, LightningManagerInit);
EI_REPLACE(0x00562DA0, VisFogManagerInit);
