#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CInterface3D;
namespace ui_gdi { struct GameDC; }
struct ItemObjectCache;

__declspec(naked) inline void __fastcall BrainResetAll(void*) EI_ORIGINAL(0x005D4F70)
__declspec(naked) inline void __fastcall Screen8Member1D8(void*) EI_ORIGINAL(0x005DB340)
__declspec(naked) inline void __fastcall PlayMovieFile(const char*, int, int, int, int, int, int)
    EI_ORIGINAL(0x005DB3F0)
__declspec(naked) inline void __fastcall ExecuteAt(const char*) EI_ORIGINAL(0x005DC620)
__declspec(naked) inline void __fastcall TrimModelsAt(ItemObjectCache*) EI_ORIGINAL(0x005F2F20)
__declspec(naked) inline void __fastcall TrimTexturesAt(ItemObjectCache*) EI_ORIGINAL(0x005F2F90)
__declspec(naked) inline void __fastcall ShowStatusText(void*, void*, const char*) EI_ORIGINAL(0x005F75B0)
__declspec(naked) inline void* __fastcall TextFormat(void* /*table*/, void*, void* /*out CString*/, int /*kind*/, const char* /*text*/)
    EI_ORIGINAL(0x005F8C90)
__declspec(naked) inline void __fastcall EmptyModelMap(void*) EI_ORIGINAL(0x005FA400)
__declspec(naked) inline void __fastcall EmptyTextureMap(void*) EI_ORIGINAL(0x005FA610)
// CUiSurface::GetDC / ReleaseDC (rewritten as UiSurfaceGetDC / UiSurfaceReleaseDC): the surface's CDC (null
// without a DirectDraw surface) and the matching release. The surface is a CUiSurface.
__declspec(naked) inline ui_gdi::GameDC* __fastcall SurfaceGetDC(void* /*surface*/) EI_ORIGINAL(0x005FDC00)
__declspec(naked) inline void __fastcall SurfaceReleaseDC(void* /*surface*/) EI_ORIGINAL(0x005FDC60)
__declspec(naked) inline void __fastcall WindowSetEnabled(void*, void*, int, int) EI_ORIGINAL(0x006002A0)
__declspec(naked) inline void __fastcall ConstructContainerAt(void*, void*, CInterface3D*) EI_ORIGINAL(0x00600B20)
__declspec(naked) inline void __fastcall DestroyContainerBody(void*) EI_ORIGINAL(0x00600BA0)
__declspec(naked) inline void __fastcall ShowWeapon(void*, void*, char*) EI_ORIGINAL(0x006229E0)
__declspec(naked) inline void __fastcall ShowArmour(void*, void*, char*) EI_ORIGINAL(0x00622A30)
__declspec(naked) inline void __fastcall ShowContainer(void*, void*, char*) EI_ORIGINAL(0x00622BC0)
__declspec(naked) inline void** __fastcall QuestIndexAt(void*, void*, const void*) EI_ORIGINAL(0x0064EE90)
