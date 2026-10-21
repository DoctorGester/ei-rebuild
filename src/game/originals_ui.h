#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CTextManager;
class CInterface3D;
class CItem;
class CUiSurface;
namespace mfc { class CString; }
namespace console { class CStringArray; }
namespace ui_gdi { struct GameDC; }
struct ItemObjectCache;
struct tagRECT;
struct tagPOINT;
struct Vector3;
class CUiWindow;
class CUnit;
class CItemScreen;
struct ItemCell;
struct NamedFloatTable;

// 00638EF0 (thiscall on the battle screen, RET 0xC; Rebirth-patched: FixSpellSelfCasting; rewritten as
// CBattleScreen::SelectUnit in ui_hud.cpp): the click on a unit or a point.
__declspec(naked) inline void __fastcall SelectUnitAt(void* /*battle screen*/, void*, CUnit*, int, int)
    EI_ORIGINAL(0x00638EF0)
__declspec(naked) inline void __fastcall Screen8Member1D8(void*) EI_ORIGINAL(0x005DB340)
__declspec(naked) inline void __fastcall ExecuteAt(const char*) EI_ORIGINAL(0x005DC620)
// CUiSurface::DrawText (RET 0x18; the surface is `this`): text into a rectangle with a format, colour and
// two more ints. Returns the DrawText result (0 without a DirectDraw surface). RECT is `tagRECT`.
__declspec(naked) inline int __fastcall SurfaceDrawText(void* /*surface*/, void*, const char* /*text*/, tagRECT* /*rect*/,
                                                        int /*format*/, std::uint32_t /*colour*/, int, int)
    EI_ORIGINAL(0x005FDE20)
__declspec(naked) inline void __fastcall DestroyContainerBody(void*) EI_ORIGINAL(0x00600BA0)
// 00458270 (__fastcall: ECX the section name, DL clears the screen first; then clear-after and two values on
// the stack, RET 0xC): plays the movies listed under a section (the titles' "<name>fin" / "<name>fout").
__declspec(naked) inline void __fastcall PlayMoviesOriginal(const char* /*section*/, int /*clearFirst*/, int, int, int)
    EI_ORIGINAL(0x00458270)
// 00600270: CUiWindow's frame time in seconds, unrounded in ST0 (ECX = window, plain RET).
__declspec(naked) inline double __fastcall WindowFrameTimeOriginal(void* /*window*/) EI_ORIGINAL(0x00600270)
// 00578D00: the flag's byte (007AFFCC) in AL; plain RET, ECX not read.

// C3i. 005E70E0: CUiMesh sprite SetRectangle(depth, scale, left, top, right, bottom, u0, v0, u1, v1) (RET 0x28, EAX not a result).
__declspec(naked) inline void __fastcall UiMeshRectangleOriginal(void* /*mesh*/, void*, float /*depth*/, float /*scale*/,
                                                                 float /*left*/, float /*top*/, float /*right*/,
                                                                 float /*bottom*/, int /*u0*/, int /*v0*/, int /*u1*/,
                                                                 int /*v1*/) EI_ORIGINAL(0x005E70E0)

// C3i part 2. 005E7680: CUiMesh sprite cells (depth, scale, left, top, right, bottom, u0, v0, u1, v1, count, flag; RET 0x30,
// EAX not a result).
__declspec(naked) inline void __fastcall UiMeshCellsOriginal(void* /*mesh*/, void*, float /*depth*/, float /*scale*/,
                                                             float /*left*/, float /*top*/, float /*right*/,
                                                             float /*bottom*/, int /*u0*/, int /*v0*/, int /*u1*/,
                                                             int /*v1*/, int /*count*/, int /*flag*/)
    EI_ORIGINAL(0x005E7680)
// 005E8450 (rewritten as CUiMeshBuilder::Framed through MeshFramed): the sprite as a framed rectangle (depth, scale, left, top,
// right, bottom, border, u0, v0, u1, v1; RET 0x2C, EAX not a result).
__declspec(naked) inline void __fastcall UiMeshFramedOriginal(void* /*mesh*/, void*, float /*depth*/, float /*scale*/,
                                                              float /*left*/, float /*top*/, float /*right*/,
                                                              float /*bottom*/, float /*border*/, int /*u0*/, int /*v0*/,
                                                              int /*u1*/, int /*v1*/) EI_ORIGINAL(0x005E8450)
// The cursor object 007C7BD8: 005E5270 shows / hides it (thiscall, RET 4: +0x0C = `show` under its lock +0xA0).
__declspec(naked) inline void __fastcall CursorShowOriginal(void* /*cursor*/, void*, int /*show*/) EI_ORIGINAL(0x005E5270)
// 006B71C0: CSoundManager::StopAll(stopStream) (thiscall on the sound manager [007AF784], RET 4;
// rewritten in sound_manager.cpp; the name is historical).

// 005E6FA0: sprite Place(x, y, z) (RET 0xC, no result).
__declspec(naked) inline void __fastcall SpritePlaceOriginal(void* /*sprite*/, void*, float, float, float) EI_ORIGINAL(0x005E6FA0)
// 006010E0: CUiContainer updates its children through slot 24 (plain RET, no result).
__declspec(naked) inline void __fastcall UiUpdateChildrenOriginal(void* /*container*/) EI_ORIGINAL(0x006010E0)

// 005E44A0 (Rebirth-patched by ImproveConsole; rewritten as ConsoleWindow::OnChar): the console's typed character
// (thiscall, RET 0xC: key, repeat, flags; no result).
__declspec(naked) inline void __fastcall ConsoleOnCharOriginal(void* /*console*/, void*, int, int, int) EI_ORIGINAL(0x005E44A0)

// 005F8340 (Rebirth-patched; thiscall, RET 0xC): the extent (a SIZE written to `out`, returned) of a text in interface
// font `font`. The text is a CString taken by value: its data pointer is handed over and the callee releases it.
__declspec(naked) inline void* __fastcall InterfaceTextExtentOriginal(CInterface3D*, void*, void* /*out SIZE*/,
                                                                      char* /*text, owned*/, int /*font*/)
    EI_ORIGINAL(0x005F8340)
// 005F7E30 (Rebirth-patched; the second copy of the screen-to-layout point scaling; thiscall, RET 4): returns the new y.
__declspec(naked) inline int __fastcall InterfacePointFromScreenCopyOriginal(const CInterface3D*, void*, tagPOINT* /*point*/)
    EI_ORIGINAL(0x005F7E30)
// 005FE2F0: CUiSurface fills its DC's surface with black (ECX, plain RET).
__declspec(naked) inline void __fastcall SurfaceClearOriginal(void* /*surface*/) EI_ORIGINAL(0x005FE2F0)
// 005187B0 (thiscall on a figure, RET 8): plays `animation` (`restart` 1: from the start). 00512B20 (thiscall on the object
// manager 007AF5F8, RET 8): its object of `kind` named `name` (an animation for kind 0x80590000, the rest pose), or null.

// A-I5. The texture manager [007AF744] (thiscall): 00691D00 its texture of `kind` named `name`, found or made (RET 8);
// 006922A0 / 006926F0 (rewritten in texture.cpp; through their address so the wrappers run) load it, from the archives or
// from `directory` (RET 4 / 8; nonzero on success); 00691D80 (rewritten as CTextureManager::NewTexture) a new CPU-filled
// texture (width, height, kind; RET 0xC).
// 00692D30 (RET 8): the registered texture of `kind` named `name` (a byte compare), else the cached one (00691BF0);
// null when neither has it.

// 0068B770 (rewritten as CSpell::IsOffensive; Rebirth's FixSpellSelfCasting patches it): an attack spell (thiscall on
// the spell, plain RET, AL).
__declspec(naked) inline bool __fastcall SpellIsOffensiveOriginal(void* /*spell*/) EI_ORIGINAL(0x0068B770)
// 006570E0: saves the game under `name` (ECX; DL the autosave flag; plain RET, no result).
__declspec(naked) inline void __fastcall AutoSaveOriginal(const char* /*name*/, int /*flag*/) EI_ORIGINAL(0x006570E0)
// 0040D3B0 (rewritten as CCamera::SetTarget; through its address so the wrapper runs): the camera follows `target`
// (thiscall on 0079B2A8, RET 4).
// 006407E0: the portraits rebuild their slots from the battle screen's party (RET 4: camera player first; no result).
__declspec(naked) inline void __fastcall PortraitsRebuildOriginal(void* /*portraits*/, void*, int /*cameraFirst*/)
    EI_ORIGINAL(0x006407E0)
// The top panel (thiscall): 0064A4E0 adds the "combat_picked_up" (or with `crippled` "combat_crippled") line of an item
// (RET 8); 0064A7A0 shows its current page (plain RET).
__declspec(naked) inline void __fastcall TopPanelShowPageOriginal(void* /*panel*/) EI_ORIGINAL(0x0064A7A0)
// 006575B0: loads the saved game `name` (ECX; plain RET; not on a network client).
__declspec(naked) inline void __fastcall LoadSavedGameOriginal(const char* /*name*/) EI_ORIGINAL(0x006575B0)
// 00566030: the client world (007AF5B8) controls `unit` (thiscall, RET 4; no result).

// A-I7. The description panel (CItemInfoPanel, thiscall; rewritten in ui_item_info.cpp): 005EE6C0
// Describe(item), 005F1590 DescribeStats(info) (each RET 4).
// 005F8070 (rewritten as CInterface3D::OnStringEvent; Rebirth's NewStringEvents patches it, so callers go through its
// address): a script string event on the interface (007C7D50; RET 4, no result).
__declspec(naked) inline void __fastcall InterfaceStringEventOriginal(void* /*interface*/, void*, const char* /*event*/)
    EI_ORIGINAL(0x005F8070)
// 004DEFA0 (the text manager; RET 8): the section names matching `pattern` ("pers *") into a CStringArray.
// 00615D30 (thiscall on the multiplayer hero screen, RET 4; Rebirth-patched, the vanilla body is
// CMpHeroScreen::SetModeVanilla): the screen laid out for mode 9, 10 or 11.
__declspec(naked) inline void __fastcall HeroScreenSetModeOriginal(void* /*screen*/, void*, int /*mode*/) EI_ORIGINAL(0x00615D30)
// 0066ED30 (rewritten as CPlayerServer::SendPosition04; RET 0xC): message 0x04, a position and two dwords. 00592090
// (rewritten as CWorldServer::CompleteBriefing; RET 8): briefing variable `name` is complete for player `playerId`.
// 00607A60 (rewritten as CBriefingScreen::PlaySpeakerAnimation; RET 0xC).
__declspec(naked) inline void __fastcall WorldCompleteBriefingOriginal(void* /*world*/, void*, int /*playerId*/,
                                                                      const char* /*name*/) EI_ORIGINAL(0x00592090)

// A-I15. The client world (007AF5B8, thiscall, RET 4): 00565D00 the unit drawn at a screen point (the point's x, y; EAX
// the object or null); 00566100 selects `unit` (null: clears the selection; no result).
// The interface's shutdown (005F4F90) calls: 00462DF0 writes config\keyboard.ini (key_bindings.cpp).
namespace keybind {
void __cdecl SaveKeyBindings();
}
// The interface's Create (005F4990) calls: 00462C10 reads config\keyboard.ini (key_bindings.cpp); 005F9810
// (thiscall on the help topics' map, RET 0xC; library) inserts a {CString, int} pair before `hint` and gives the
// new node in `out`, which it returns.
namespace keybind {
void __cdecl LoadKeyBindings();
}
// 005E3700 (thiscall on the chat overlay, plain RET; Rebirth-patched): the typed line drawn on its surface (hidden while
// the chat is closed), the caret clamped to it.
__declspec(naked) inline void __fastcall ChatOverlayDrawTypedOriginal(void* /*chat*/) EI_ORIGINAL(0x005E3700)
// 00661FE0 (thiscall on a CParty, plain RET): every item released (its slot 4), the item array's storage freed.
__declspec(naked) inline void __fastcall PartyReleaseItemsOriginal(void* /*party*/) EI_ORIGINAL(0x00661FE0)

// A-E2. 005DAEA0 (fastcall, RET 8; rewritten as SetUpCursors): the cursor (007C7BD8) initialised and given its shapes.
void __fastcall SetUpCursors(bool /*modeTwo*/, int /*context*/, int /*smallCursors*/, int /*flags*/);
// 005DAE70 (fastcall, RET 8; cursor_setup.cpp): the cursor shut down and set up again.
void __fastcall ResetCursors(bool /*modeTwo*/, int /*context*/, int /*smallCursors*/, int /*flags*/);


// C3o. 004DF030 (rewritten as TextFindLine; thiscall on the text reader, RET 4): moves to the line that starts with
// `key`; a null key finds the next "#..." header. 1 when found, else 0 (the cursor at the end).
__declspec(naked) inline int __fastcall TextFindLineOriginal(void* /*texts*/, void*, const char* /*key*/)
    EI_ORIGINAL(0x004DF030)

// C3n batch 4. 005F7590 (rewritten as CInterface3D::GetActiveWindow; plain RET): the interface's top window, null with none.
// 005F7DA0 (CInterface3D::ScaleRectFromScreen; RET 4): the pixel rectangle back to 800x600 layout coordinates (returns the
// new bottom). Rebirth patches both: through their addresses.
__declspec(naked) inline CUiWindow* __fastcall InterfaceActiveWindowOriginal(CInterface3D* /*interface*/) EI_ORIGINAL(0x005F7590)
__declspec(naked) inline int __fastcall InterfaceScaleRectFromScreenOriginal(const CInterface3D* /*interface*/, void*,
                                                                              tagRECT* /*rect*/) EI_ORIGINAL(0x005F7DA0)
