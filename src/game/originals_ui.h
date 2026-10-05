#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CInterface3D;
namespace ui_gdi { struct GameDC; }
struct ItemObjectCache;
struct tagRECT;

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
// CUiSurface::DrawText (RET 0x18; the surface is `this`): text into a rectangle with a format, colour and
// two more ints. Returns the DrawText result (0 without a DirectDraw surface). RECT is `tagRECT`.
__declspec(naked) inline int __fastcall SurfaceDrawText(void* /*surface*/, void*, const char* /*text*/, tagRECT* /*rect*/,
                                                        int /*format*/, std::uint32_t /*colour*/, int, int)
    EI_ORIGINAL(0x005FDE20)
__declspec(naked) inline void __fastcall WindowSetEnabled(void*, void*, int, int) EI_ORIGINAL(0x006002A0)
__declspec(naked) inline void __fastcall ConstructContainerAt(void*, void*, CInterface3D*) EI_ORIGINAL(0x00600B20)
__declspec(naked) inline void __fastcall DestroyContainerBody(void*) EI_ORIGINAL(0x00600BA0)
__declspec(naked) inline void __fastcall ShowWeapon(void*, void*, char*) EI_ORIGINAL(0x006229E0)
__declspec(naked) inline void __fastcall ShowArmour(void*, void*, char*) EI_ORIGINAL(0x00622A30)
__declspec(naked) inline void __fastcall ShowContainer(void*, void*, char*) EI_ORIGINAL(0x00622BC0)
__declspec(naked) inline void** __fastcall QuestIndexAt(void*, void*, const void*) EI_ORIGINAL(0x0064EE90)
// 005E5200: the mouse cursor (007C7BD8) SetShape(shape), RET 4, returns the previous shape or -1.
__declspec(naked) inline int __fastcall CursorSetShapeOriginal(void* /*cursor*/, void*, int /*shape*/) EI_ORIGINAL(0x005E5200)
// 00634650: constructs a message box window in `memory` (RET 0x18, returns it).
__declspec(naked) inline void* __fastcall MessageBoxConstructOriginal(void* /*memory*/, void*, CInterface3D*,
                                                                      const char*, const char*, int, int, int)
    EI_ORIGINAL(0x00634650)
// 00600270: CUiWindow's frame time in seconds, unrounded in ST0 (ECX = window, plain RET).
__declspec(naked) inline double __fastcall WindowFrameTimeOriginal(void* /*window*/) EI_ORIGINAL(0x00600270)
// 00578D10: sets the world server's flag through the game state at 007AFEA4. A free function: one stack argument,
// RET 4, ECX is not read (the callers' member `this` is the state but is ignored); EAX is not a result.
__declspec(naked) inline void __stdcall SetServerFlagOriginal(int /*flag*/) EI_ORIGINAL(0x00578D10)

// C3i. 005E70E0: CUiMesh sprite SetRectangle(depth, scale, left, top, right, bottom, u0, v0, u1, v1) (RET 0x28, EAX not a result).
__declspec(naked) inline void __fastcall UiMeshRectangleOriginal(void* /*mesh*/, void*, float /*depth*/, float /*scale*/,
                                                                 float /*left*/, float /*top*/, float /*right*/,
                                                                 float /*bottom*/, int /*u0*/, int /*v0*/, int /*u1*/,
                                                                 int /*v1*/) EI_ORIGINAL(0x005E70E0)

// C3i part 2. 005E70B0: sprite scale, unrounded in ST0 (ECX = sprite, plain RET). 005E7680: CUiMesh sprite cells (depth, scale,
// left, top, right, bottom, u0, v0, u1, v1, count, flag; RET 0x30, EAX not a result).
__declspec(naked) inline double __fastcall SpriteScaleOriginal(const void* /*sprite*/) EI_ORIGINAL(0x005E70B0)
__declspec(naked) inline void __fastcall UiMeshCellsOriginal(void* /*mesh*/, void*, float /*depth*/, float /*scale*/,
                                                             float /*left*/, float /*top*/, float /*right*/,
                                                             float /*bottom*/, int /*u0*/, int /*v0*/, int /*u1*/,
                                                             int /*v1*/, int /*count*/, int /*flag*/)
    EI_ORIGINAL(0x005E7680)
// The cursor object 007C7BD8: 005E5270 shows / hides it (thiscall, RET 4: +0x0C = `show` under its lock +0xA0);
// 005E65F0 / 005E6610 enter / leave its lock +0xA0 in mode 2 (ECX, plain RET).
__declspec(naked) inline void __fastcall CursorShowOriginal(void* /*cursor*/, void*, int /*show*/) EI_ORIGINAL(0x005E5270)
__declspec(naked) inline void __fastcall CursorLockOriginal(void* /*cursor*/) EI_ORIGINAL(0x005E65F0)
__declspec(naked) inline void __fastcall CursorUnlockOriginal(void* /*cursor*/) EI_ORIGINAL(0x005E6610)
// 006B71C0: the loading screen's step (thiscall on [007AF784], RET 4).
__declspec(naked) inline void __fastcall LoadingStepOriginal(void* /*loading*/, void*, int /*step*/) EI_ORIGINAL(0x006B71C0)
// 005E6630 (rewritten as CursorSaveBackground, ui_cursor_frame.cpp; called through its address so the wrapper
// runs): the cursor saves the screen under its drawn rectangle into its background surface, from the primary
// (`fromPrimary`) or the render target (thiscall, RET 4).
__declspec(naked) inline void __fastcall CursorSaveBackgroundOriginal(void* /*cursor*/, void*, char /*fromPrimary*/)
    EI_ORIGINAL(0x005E6630)

// C3j. 005E7060: sprite SetScale (RET 4; the float is only copied to three fields, so it is raw dword bits; no result).
__declspec(naked) inline void __fastcall SpriteSetScaleOriginal(void* /*sprite*/, void*, std::uint32_t /*scaleBits*/)
    EI_ORIGINAL(0x005E7060)
// 005E6FA0: sprite Place(x, y, z) (RET 0xC, no result).
__declspec(naked) inline void __fastcall SpritePlaceOriginal(void* /*sprite*/, void*, float, float, float) EI_ORIGINAL(0x005E6FA0)
// 006010E0: CUiContainer updates its children through slot 24 (plain RET, no result).
__declspec(naked) inline void __fastcall UiUpdateChildrenOriginal(void* /*container*/) EI_ORIGINAL(0x006010E0)
// 0063B920: battle screen shows the panel of a unit (RET 8: unit, extra; no result).
__declspec(naked) inline void __fastcall BattleShowPanelUnitOriginal(void* /*screen*/, void*, void* /*unit*/, void* /*extra*/)
    EI_ORIGINAL(0x0063B920)
// 0063CD20 / 0063E750 / 0063F730: quick, spell and action panel slot updates (RET 8: slot, flag; no result).
__declspec(naked) inline void __fastcall QuickPanelSetOriginal(void* /*panel*/, void*, int, int) EI_ORIGINAL(0x0063CD20)
__declspec(naked) inline void __fastcall SpellPanelSetOriginal(void* /*panel*/, void*, int, int) EI_ORIGINAL(0x0063E750)
__declspec(naked) inline void __fastcall ActionPanelSetOriginal(void* /*panel*/, void*, int, int) EI_ORIGINAL(0x0063F730)
// 0065CF30: appends a line to a log window (RET 4, no result).
__declspec(naked) inline void __fastcall LogAddTextOriginal(void* /*window*/, void*, const char* /*text*/) EI_ORIGINAL(0x0065CF30)
// 005EE050 / 005EE080 / 005EE0D0: item record is a spell / modifier / part (RET 4, AL; free functions, ECX is not read, so the
// members that forwarded them ignored their `this`).
__declspec(naked) inline bool __stdcall ItemKindIsSpellOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE050)
__declspec(naked) inline bool __stdcall ItemKindIsModifierOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE080)
__declspec(naked) inline bool __stdcall ItemKindIsPartOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE0D0)
// 005E0DB0: variable table iterator: the next node in the bucket chain or null (plain RET, EAX = node).
__declspec(naked) inline void* __fastcall VariableIteratorNextOriginal(void* /*iterator*/) EI_ORIGINAL(0x005E0DB0)

// C3l. 005F74F0: the interface closes its pending windows (ECX = the interface; walks the list at +0x34..+0x38, clears the five
// flag bytes at +0x7C..+0x80; plain RET, EAX is not a result).
__declspec(naked) inline void __fastcall InterfaceCloseAllOriginal(void* /*interface*/) EI_ORIGINAL(0x005F74F0)
