#pragma once

// The constant virtuals of the engine's small tables that are still free functions (world_class_slots.cpp):
// class names and ids and the interface objects' base. The worlds' own virtuals are their classes' members
// (world.h), defined in world_class_slots.cpp too. docs/notes/world.md.

#include <cstdint>

class CEngineObject;

// GetClassName / GetClassId of the vtable named in the comment.
const char* I3DFigureClassName(CEngineObject*);           // "CI3DFigure"
int I3DFigureClassId(CEngineObject*);                     // 0x46
const char* FigureClientSpecificClassName(CEngineObject*);   // "CFigureClientSpecific"
int FigureClientSpecificClassId(CEngineObject*);          // 0x36
// The interface objects' base (53 window classes keep these): Create answers 1, the name is "Sorry, no
// other way.", the id 0x45.
int UiObjectCreate(CEngineObject*);
const char* UiObjectClassName(CEngineObject*);
int UiObjectClassId(CEngineObject*);

// The lightning and fog managers' Init is CBaseManager::Init; the particle, lightning and fog
// managers' class names and ids, the item manager's folder name are their classes' overrides.
