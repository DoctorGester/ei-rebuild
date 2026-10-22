#pragma once

// The constant slots of the engine's small tables that are still free functions (A-W14;
// world_class_slots.cpp): class names (slot 11) and ids (slot 12) and the interface objects' base.
// The worlds' own slots are their classes' members (world.h), defined in world_class_slots.cpp too.
// docs/notes/world.md.

#include <cstdint>

class CBaseManager;
class CMaterialManager;
class CEngineObject;
class CFigureManager;
class CLightningManager;
struct CItemManager;
class CParticleManager;
class CTextureManager;
struct PlatformSet;

// Slot 11 / 12 of the vtable named in the comment.
const char* __fastcall BridgeManagerClassName(PlatformSet*);         // 00561870 (0073F4FC) "CBridgeManager"
int __fastcall BridgeManagerClassId(PlatformSet*);                   // 00561880: 0x34
const char* __fastcall I3DFigureClassName(CEngineObject*);           // 005AACC0 (0073FC48) "CI3DFigure"
int __fastcall I3DFigureClassId(CEngineObject*);                     // 005AACD0: 0x46
const char* __fastcall FigureClientSpecificClassName(CEngineObject*);   // 005A7040 (0073FB10) "CFigureClientSpecific"
int __fastcall FigureClientSpecificClassId(CEngineObject*);          // 005A7050: 0x36
// The interface objects' base (vtable 0073F7F8; 53 window classes at 007400DC .. 00742F64 keep
// these slots): slot 2 answers 1, the name is "Sorry, no other way.", the id 0x45.
int __fastcall UiObjectCreate(CEngineObject*);                       // 00579560
const char* __fastcall UiObjectClassName(CEngineObject*);            // 00579580
int __fastcall UiObjectClassId(CEngineObject*);                      // 00579590

// 00562D70 / 00562DA0 (RET 4): slot 13 (Init) of the lightning and fog managers is CBaseManager::Init
// (004031B0); the particle, lightning and fog managers' class names and ids, the item manager's folder name
// (0055F3B0) are their classes' overrides.
