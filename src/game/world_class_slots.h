#pragma once

// The constant virtuals of the engine's small tables that are still free functions (world_class_slots.cpp):
// class names and ids. The worlds' own virtuals are their classes' members
// (world.h), defined in world_class_slots.cpp too. docs/notes/world.md.

#include <cstdint>

class CEngineObject;

// ClassName of the vtable named in the comment.
const char* FigureClientSpecificClassName(CEngineObject*);   // "CFigureClientSpecific"

// The lightning and fog managers' Init is CBaseManager::Init; the particle, lightning and fog
// managers' class names and ids, the item manager's folder name are their classes' overrides.
