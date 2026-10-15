// Checks for world_class_slots.cpp (A-W14).
// - Class names and ids, the resource folder names and the slots answering 1: MOV EAX,imm32
//   (5 bytes), pure, against the original.
// - The client world's getters (MOV EAX,[ECX+disp32], 6 bytes): pure, against the original.
// - The grid moves (slots 41 / 42) and the two managers' Init thunks: ours only; the object's
//   stored position / the manager's context checked.
// Reached: the names and ids whenever a manager or the world is listed or saved (every start-up
// registers the managers; the debug display lists them), the client's getters all the time (the
// texture and particle managers are fetched through the world's slots), slot 41 whenever an object
// moves on the client, the Init thunks at the client's Setup (every start-up).
#include "verify.h"

#include "../../src/game/world.h"
#include "../../src/game/world_class_slots.h"

namespace {

void* s_unused;

// A pure function of `self` alone, compared with the original.
#define EI_PURE_SLOT(Result, Self, Function)                                                         \
    Result(__fastcall* s_##Function)(Self*);                                                         \
    Result __fastcall Function##Verify(Self* self) {                                                 \
        Result expected = s_##Function(self);                                                        \
        ei::verify::Check(#Function, expected, Function(self), "self %p", self);                     \
        return expected;                                                                             \
    }

EI_PURE_SLOT(const char*, CWorldClient, WorldClientClassName)
EI_PURE_SLOT(int, CWorldClient, WorldClientClassId)
EI_PURE_SLOT(const char*, CWorldServer, WorldServerClassName)
EI_PURE_SLOT(int, CWorldServer, WorldServerClassId)
EI_PURE_SLOT(const char*, PlatformSet, BridgeManagerClassName)
EI_PURE_SLOT(int, PlatformSet, BridgeManagerClassId)
EI_PURE_SLOT(const char*, CParticleManager, ParticleManagerClassName)
EI_PURE_SLOT(int, CParticleManager, ParticleManagerClassId)
EI_PURE_SLOT(const char*, ClientTexts, TextManagerClassName)
EI_PURE_SLOT(int, ClientTexts, TextManagerClassId)
EI_PURE_SLOT(const char*, Terrain, TerrainClassName)
EI_PURE_SLOT(int, Terrain, TerrainClassId)
EI_PURE_SLOT(const char*, ClientListManager, LightningManagerClassName)
EI_PURE_SLOT(int, ClientListManager, LightningManagerClassId)
EI_PURE_SLOT(const char*, ClientArrayManager, VisFogManagerClassName)
EI_PURE_SLOT(int, ClientArrayManager, VisFogManagerClassId)
EI_PURE_SLOT(const char*, CFigureManager, FigureManagerClassName)
EI_PURE_SLOT(int, CFigureManager, FigureManagerClassId)
EI_PURE_SLOT(const char*, CEngineObject, I3DFigureClassName)
EI_PURE_SLOT(int, CEngineObject, I3DFigureClassId)
EI_PURE_SLOT(const char*, CEngineObject, FigureClientSpecificClassName)
EI_PURE_SLOT(int, CEngineObject, FigureClientSpecificClassId)
EI_PURE_SLOT(int, CEngineObject, UiObjectCreate)
EI_PURE_SLOT(const char*, CEngineObject, UiObjectClassName)
EI_PURE_SLOT(int, CEngineObject, UiObjectClassId)
EI_PURE_SLOT(const char*, CItemManager, ItemManagerResourceName)
EI_PURE_SLOT(const char*, CFigureManager, FigureManagerResourceName)
EI_PURE_SLOT(int, CWorld, WorldBaseRunTick)
EI_PURE_SLOT(int, CWorldServer, WorldServerIsServer)
EI_PURE_SLOT(int, CEngineObject, PartyBaseCreate)
EI_PURE_SLOT(const char*, CWorldClient, WorldClientTexturePath)
EI_PURE_SLOT(CBaseManager*, CWorldClient, WorldClientMaterialManager)
EI_PURE_SLOT(CTextureManager*, CWorldClient, WorldClientTextureManager)
EI_PURE_SLOT(CBaseManager*, CWorldClient, WorldClientLightManager)
EI_PURE_SLOT(CParticleManager*, CWorldClient, WorldClientParticleManager)
EI_PURE_SLOT(CEngineObject*, CWorldClient, WorldClientManager194)
EI_PURE_SLOT(CEngineObject*, CWorldClient, WorldClientManager198)

// The object's position (+0x1C, three floats) is the one handed in afterwards; a position that
// holds a NaN is compared by bits.
bool StoredAt(CEngineObject* object, const float* position) {
    return std::memcmp(reinterpret_cast<const std::uint8_t*>(object) + sizeof(CEngineObject), position, 3 * sizeof(float)) == 0;
}
void __fastcall MoveObjectVerify(CWorldClient* self, void*, CEngineObject* object, const float* position) {
    self->MoveObjectImpl(object, position);
    ei::verify::Check("WorldClientMoveObject", true, StoredAt(object, position), "object %p", object);
}
void __fastcall MoveObject2Verify(CWorldClient* self, void*, CEngineObject* object, const float* position) {
    self->MoveObject2Impl(object, position);
    ei::verify::Check("WorldClientMoveObject2", true, StoredAt(object, position), "object %p", object);
}

int __fastcall LightningInitVerify(ClientListManager* self, void*, void* context) {
    int result = self->CBaseManager::InitImpl(context);
    ei::verify::Check("LightningManagerInit", true, self->context_ == context, "manager %p result %d", self, result);
    return result;
}
int __fastcall VisFogInitVerify(ClientArrayManager* self, void*, void* context) {
    int result = self->CBaseManager::InitImpl(context);
    ei::verify::Check("VisFogManagerInit", true, self->context_ == context, "manager %p result %d", self, result);
    return result;
}

} // namespace

EI_VERIFY(0x005619E0, WorldClientClassNameVerify, 5, s_WorldClientClassName);
EI_VERIFY(0x005619F0, WorldClientClassIdVerify, 5, s_WorldClientClassId);
EI_VERIFY(0x0058A410, WorldServerClassNameVerify, 5, s_WorldServerClassName);
EI_VERIFY(0x0058A420, WorldServerClassIdVerify, 5, s_WorldServerClassId);
EI_VERIFY(0x00561870, BridgeManagerClassNameVerify, 5, s_BridgeManagerClassName);
EI_VERIFY(0x00561880, BridgeManagerClassIdVerify, 5, s_BridgeManagerClassId);
EI_VERIFY(0x00562780, ParticleManagerClassNameVerify, 5, s_ParticleManagerClassName);
EI_VERIFY(0x00562790, ParticleManagerClassIdVerify, 5, s_ParticleManagerClassId);
EI_VERIFY(0x00562970, TextManagerClassNameVerify, 5, s_TextManagerClassName);
EI_VERIFY(0x00562980, TextManagerClassIdVerify, 5, s_TextManagerClassId);
EI_VERIFY(0x00562B40, TerrainClassNameVerify, 5, s_TerrainClassName);
EI_VERIFY(0x00562B50, TerrainClassIdVerify, 5, s_TerrainClassId);
EI_VERIFY(0x00562D80, LightningManagerClassNameVerify, 5, s_LightningManagerClassName);
EI_VERIFY(0x00562D90, LightningManagerClassIdVerify, 5, s_LightningManagerClassId);
EI_VERIFY(0x00562DB0, VisFogManagerClassNameVerify, 5, s_VisFogManagerClassName);
EI_VERIFY(0x00562DC0, VisFogManagerClassIdVerify, 5, s_VisFogManagerClassId);
EI_VERIFY(0x005AA180, FigureManagerClassNameVerify, 5, s_FigureManagerClassName);
EI_VERIFY(0x005AA190, FigureManagerClassIdVerify, 5, s_FigureManagerClassId);
EI_VERIFY(0x005AACC0, I3DFigureClassNameVerify, 5, s_I3DFigureClassName);
EI_VERIFY(0x005AACD0, I3DFigureClassIdVerify, 5, s_I3DFigureClassId);
EI_VERIFY(0x005A7040, FigureClientSpecificClassNameVerify, 5, s_FigureClientSpecificClassName);
EI_VERIFY(0x005A7050, FigureClientSpecificClassIdVerify, 5, s_FigureClientSpecificClassId);
EI_VERIFY(0x00579560, UiObjectCreateVerify, 5, s_UiObjectCreate);
EI_VERIFY(0x00579580, UiObjectClassNameVerify, 5, s_UiObjectClassName);
EI_VERIFY(0x00579590, UiObjectClassIdVerify, 5, s_UiObjectClassId);
EI_VERIFY(0x0055F3B0, ItemManagerResourceNameVerify, 5, s_ItemManagerResourceName);
EI_VERIFY(0x005AA170, FigureManagerResourceNameVerify, 5, s_FigureManagerResourceName);
EI_VERIFY(0x0055FAD0, WorldBaseRunTickVerify, 5, s_WorldBaseRunTick);
EI_VERIFY(0x0058A380, WorldServerIsServerVerify, 5, s_WorldServerIsServer);
EI_VERIFY(0x0055E930, PartyBaseCreateVerify, 5, s_PartyBaseCreate);
EI_VERIFY(0x00561890, WorldClientTexturePathVerify, 6, s_WorldClientTexturePath);
EI_VERIFY(0x00561980, WorldClientMaterialManagerVerify, 6, s_WorldClientMaterialManager);
EI_VERIFY(0x00561990, WorldClientTextureManagerVerify, 6, s_WorldClientTextureManager);
EI_VERIFY(0x005619A0, WorldClientLightManagerVerify, 6, s_WorldClientLightManager);
EI_VERIFY(0x005619B0, WorldClientParticleManagerVerify, 6, s_WorldClientParticleManager);
EI_VERIFY(0x005619C0, WorldClientManager194Verify, 6, s_WorldClientManager194);
EI_VERIFY(0x005619D0, WorldClientManager198Verify, 6, s_WorldClientManager198);
EI_VERIFY(0x005618A0, MoveObjectVerify, 0, s_unused);
EI_VERIFY(0x005618C0, MoveObject2Verify, 0, s_unused);
EI_VERIFY(0x00562D70, LightningInitVerify, 0, s_unused);
EI_VERIFY(0x00562DA0, VisFogInitVerify, 0, s_unused);
