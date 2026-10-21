#pragma once

// The constant slots and plain getters of the world classes (A-W14; world_class_slots.cpp): class
// names (slot 11) and ids (slot 12), slots that only answer 1, the client world's manager getters
// (slots 25, 33-38) and its grid moves (slots 41 / 42), and the managers' slot 13 thunks.
// docs/notes/world.md.

#include <cstdint>

class CBaseManager;
class CEngineObject;
class CFigureManager;
class CLightningManager;
struct CItemManager;
class CParticleManager;
class CTextureManager;
class CWorld;
class CWorldClient;
class CWorldServer;
class Terrain;
struct ClientArrayManager;
struct ClientTexts;
struct PlatformSet;

// Slot 11 / 12 of the vtable named in the comment.
const char* __fastcall WorldClientClassName(CWorldClient*);          // 005619E0 (0073F41C) "CWorldClient"
int __fastcall WorldClientClassId(CWorldClient*);                    // 005619F0: 7
const char* __fastcall WorldServerClassName(CWorldServer*);          // 0058A410 (0073F928) "CWorldServer"
int __fastcall WorldServerClassId(CWorldServer*);                    // 0058A420: 8
const char* __fastcall BridgeManagerClassName(PlatformSet*);         // 00561870 (0073F4FC) "CBridgeManager"
int __fastcall BridgeManagerClassId(PlatformSet*);                   // 00561880: 0x34
const char* __fastcall ParticleManagerClassName(CParticleManager*);  // 00562780 (0073F644) "CParticleManager"
int __fastcall ParticleManagerClassId(CParticleManager*);            // 00562790: 0x2C
const char* __fastcall TextManagerClassName(ClientTexts*);           // 00562970 (0073F6F0) "CTextManager"
int __fastcall TextManagerClassId(ClientTexts*);                     // 00562980: 0x2A
const char* __fastcall TerrainClassName(Terrain*);                   // 00562B40 (0073F6A8) "CTerrain"
int __fastcall TerrainClassId(Terrain*);                             // 00562B50: 0xB
const char* __fastcall LightningManagerClassName(CLightningManager*);   // 00562D80 "CLightningManager"
int __fastcall LightningManagerClassId(CLightningManager*);          // 00562D90: 0x35
const char* __fastcall VisFogManagerClassName(ClientArrayManager*);  // 00562DB0 "CVisFogManager"
int __fastcall VisFogManagerClassId(ClientArrayManager*);            // 00562DC0: 0x60
const char* __fastcall I3DFigureClassName(CEngineObject*);           // 005AACC0 (0073FC48) "CI3DFigure"
int __fastcall I3DFigureClassId(CEngineObject*);                     // 005AACD0: 0x46
const char* __fastcall FigureClientSpecificClassName(CEngineObject*);   // 005A7040 (0073FB10) "CFigureClientSpecific"
int __fastcall FigureClientSpecificClassId(CEngineObject*);          // 005A7050: 0x36
// The interface objects' base (vtable 0073F7F8; 53 window classes at 007400DC .. 00742F64 keep
// these slots): slot 2 answers 1, the name is "Sorry, no other way.", the id 0x45.
int __fastcall UiObjectCreate(CEngineObject*);                       // 00579560
const char* __fastcall UiObjectClassName(CEngineObject*);            // 00579580
int __fastcall UiObjectClassId(CEngineObject*);                      // 00579590

// Slot 25 of a resource manager: the name of its resource folder.
const char* __fastcall ItemManagerResourceName(CItemManager*);       // 0055F3B0 (0073F25C) "items"

// 0055FAD0: slot 6 (RunTick) of the worlds' base vtable 0073F18C answers 1.
int __fastcall WorldBaseRunTick(CWorld*);
// 0058A380: the server's slot 26 (IsServer) answers 1.
int __fastcall WorldServerIsServer(CWorldServer*);
// 0055E930: slot 2 (Create) of CParty's base vtable 0074306C answers 1 (CParty::Create 006614F0
// also calls it directly).
int __fastcall PartyBaseCreate(CEngineObject*);

// The client world's getters: slot 25 the texture folder's text (+0x224), 33 the material manager
// (+0x188), 34 the texture manager (+0x18C), 35 the light manager (+0x190), 36 the particle
// manager (+0x19C), 37 / 38 the managers at +0x194 / +0x198.
const char* __fastcall WorldClientTexturePath(CWorldClient*);        // 00561890
CBaseManager* __fastcall WorldClientMaterialManager(CWorldClient*);  // 00561980
CTextureManager* __fastcall WorldClientTextureManager(CWorldClient*);   // 00561990
CBaseManager* __fastcall WorldClientLightManager(CWorldClient*);     // 005619A0
CParticleManager* __fastcall WorldClientParticleManager(CWorldClient*);   // 005619B0
CEngineObject* __fastcall WorldClientManager194(CWorldClient*);      // 005619C0
CEngineObject* __fastcall WorldClientManager198(CWorldClient*);      // 005619D0

// 00562D70 / 00562DA0 (RET 4): slot 13 (Init) of the lightning and fog managers is CBaseManager::InitImpl
// (004031B0), registered at both addresses in world_class_slots.cpp.
