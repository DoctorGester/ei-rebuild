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
__declspec(naked) inline void __fastcall PlayMovieFile(const char*, int, int, int, int, int, int)
    EI_ORIGINAL(0x005DB3F0)
__declspec(naked) inline void __fastcall ExecuteAt(const char*) EI_ORIGINAL(0x005DC620)
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
__declspec(naked) inline void __fastcall ConstructContainerAt(void*, void*, CInterface3D*) EI_ORIGINAL(0x00600B20)
__declspec(naked) inline void __fastcall DestroyContainerBody(void*) EI_ORIGINAL(0x00600BA0)
__declspec(naked) inline void __fastcall ShowWeapon(void*, void*, char*) EI_ORIGINAL(0x006229E0)
__declspec(naked) inline void __fastcall ShowArmour(void*, void*, char*) EI_ORIGINAL(0x00622A30)
__declspec(naked) inline void __fastcall ShowContainer(void*, void*, char*) EI_ORIGINAL(0x00622BC0)
__declspec(naked) inline void** __fastcall QuestIndexAt(void*, void*, const void*) EI_ORIGINAL(0x0064EE90)
// The edit field (CUiEditField, ui.h): 0065C010 constructor (RET 0x14: the interface and a RECT by value, passed here
// as its four ints; returns this), 0065C1A0 its look (RET 0x18: two ints, the font, an int, the text and caret
// colours), 0065C290 its text (RET 4).
__declspec(naked) inline void* __fastcall EditFieldConstructOriginal(void* /*memory*/, void*, CInterface3D*, int /*left*/,
                                                                     int /*top*/, int /*right*/, int /*bottom*/)
    EI_ORIGINAL(0x0065C010)
// 00634650: constructs a message box window in `memory` (RET 0x18, returns it).
__declspec(naked) inline void* __fastcall MessageBoxConstructOriginal(void* /*memory*/, void*, CInterface3D*,
                                                                      const char*, const char*, int, int, int)
    EI_ORIGINAL(0x00634650)
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
// 005EE050 / 005EE080 / 005EE0D0: item record is a spell / modifier / part (RET 4, AL; free functions, ECX is not read, so the
// members that forwarded them ignored their `this`).
__declspec(naked) inline bool __stdcall ItemKindIsSpellOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE050)
__declspec(naked) inline bool __stdcall ItemKindIsModifierOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE080)
__declspec(naked) inline bool __stdcall ItemKindIsPartOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE0D0)
// 005E0DB0: variable table iterator: the next node in the bucket chain or null (plain RET, EAX = node).
__declspec(naked) inline void* __fastcall VariableIteratorNextOriginal(void* /*iterator*/) EI_ORIGINAL(0x005E0DB0)

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
// 005FCDD0: CUiSurface constructor in `memory` over the layout rectangle `rect` with format `format` (thiscall, RET 0xC,
// returns `memory`; string_map.cpp and ui_console_window.cpp still have their own forwarders).
__declspec(naked) inline CUiSurface* __fastcall SurfaceConstructOnRectOriginal(void* /*memory*/, void*,
                                                                                    CInterface3D* /*ui*/,
                                                                                    const tagRECT* /*rect*/,
                                                                                    int /*format*/) EI_ORIGINAL(0x005FCDD0)
// 005FDA80: CUiSurface's own draw (slot 0; rewritten as UiSurfaceBlit), for the classes that draw over it.
__declspec(naked) inline void __fastcall SurfaceBlitOriginal(void* /*surface*/) EI_ORIGINAL(0x005FDA80)
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
__declspec(naked) inline std::uint8_t* __fastcall NewManagedTextureOriginal(void* /*manager*/, void*, int /*width*/,
                                                                            int /*height*/, int /*kind*/)
    EI_ORIGINAL(0x00691D80)

// 005A6C70 (a jump to 0059F210): the figure's Create (vtable slot 2; thiscall, plain RET; the result is not used).
__declspec(naked) inline void __fastcall FigureCreateOriginal(void* /*figure*/) EI_ORIGINAL(0x005A6C70)
// 005A6C80: the figure's slot 3 (its three arrays emptied, the parts' storage freed, then 0059F2B0, whose EAX is left;
// thiscall, plain RET).
__declspec(naked) inline int __fastcall FigureDestroyOriginal(void* /*figure*/) EI_ORIGINAL(0x005A6C80)
// 007022A0: MFC's AfxBeginThread(procedure, parameter, priority, stack size, flags, security) (__stdcall; returns the
// CWinThread).
__declspec(naked) inline void* __stdcall AfxBeginThreadOriginal(void* /*procedure*/, void* /*parameter*/, int /*priority*/,
                                                                unsigned /*stack*/, unsigned long /*flags*/,
                                                                void* /*security*/) EI_ORIGINAL(0x007022A0)
// 0068B770 (rewritten as CSpell::IsOffensive; Rebirth's FixSpellSelfCasting patches it): an attack spell (thiscall on
// the spell, plain RET, AL).
__declspec(naked) inline bool __fastcall SpellIsOffensiveOriginal(void* /*spell*/) EI_ORIGINAL(0x0068B770)
// 006570E0: saves the game under `name` (ECX; DL the autosave flag; plain RET, no result).
__declspec(naked) inline void __fastcall AutoSaveOriginal(const char* /*name*/, int /*flag*/) EI_ORIGINAL(0x006570E0)
// 0040D3B0 (rewritten as CCamera::SetTarget; through its address so the wrapper runs): the camera follows `target`
// (thiscall on 0079B2A8, RET 4).
// 005EBDB0: the battle screen's party array (an MFC CArray of unit pointers) SetSize (RET 8; growBy -1 keeps the step).
__declspec(naked) inline void __fastcall PartyArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x005EBDB0)
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
// 005EC300 (rewritten as CUiWindow::DrawHeroPreview): the hero preview (RET 0x14: pass, model, turn, depth, wide).
__declspec(naked) inline void __fastcall WindowDrawHeroPreviewOriginal(void* /*window*/, void*, int /*pass*/,
                                                                       void* /*model*/, float /*turn*/,
                                                                       float /*depth*/, int /*wide*/)
    EI_ORIGINAL(0x005EC300)
// The multiplayer hero screen (thiscall, plain RET): 006167C0 draws the hero list, 00616560 the voice list.
__declspec(naked) inline void __fastcall HeroScreenDrawListOriginal(void* /*screen*/) EI_ORIGINAL(0x006167C0)
// 0065A770 (library, RET 4): the interface's help topics (map<CString, int>, CInterface3D +0x98) operator[]: the
// topic's value, a new entry with 0 when absent.
__declspec(naked) inline int* __fastcall HelpTopicsAtOriginal(void* /*topics*/, void*, const mfc::CString* /*topic*/)
    EI_ORIGINAL(0x0065A770)
// The camp's item helpers (rewritten in ui_screens.cpp / ui_item_screen.cpp; called through their address so the wrappers
// run). 005EE0B0 / 005EE130: the item is a spell container (all of EAX) / can be taken apart (AL) (free functions, RET 4;
// ECX not read). 005EE2C0: CItemScreen::ReturnAll(to, from) (RET 8, the last item moved). 005EE1B0: MoveItem(item, to,
// from, limit, merge, turning) (RET 0x18, what `to` now holds).
__declspec(naked) inline int __stdcall ItemKindIsSpellContainerOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE0B0)
__declspec(naked) inline bool __stdcall ItemKindIsBuildableOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE130)
// 005EE110: the item is a material (class 0x3007, kind 1; RET 4, AL; ECX not read).
__declspec(naked) inline bool __stdcall ItemKindIsKindOneOriginal(const void* /*item*/) EI_ORIGINAL(0x005EE110)
__declspec(naked) inline CItem* __fastcall ItemScreenMoveItemOriginal(void* /*screen*/, void*, CItem* /*item*/, void* /*to*/,
                                                                      void* /*from*/, int /*limit*/, int /*merge*/,
                                                                      int /*turning*/) EI_ORIGINAL(0x005EE1B0)
// STLport containers' storage (library code): 006344C0 / 00520F60 give `count` entries of 8 / 4 bytes back to the node
// pool (__fastcall: block, count); 00520F20 the same for 4-byte entries through an allocator proxy (thiscall, RET 8: block,
// count). 00634160 / 00634200: the hero screen's model / texture map destroyed (nodes erased, header freed; ECX).
__declspec(naked) inline void __fastcall FreeBlock8Original(void* /*block*/, int /*count*/) EI_ORIGINAL(0x006344C0)
__declspec(naked) inline void __fastcall FreeBlock4Original(void* /*block*/, int /*count*/) EI_ORIGINAL(0x00520F60)
__declspec(naked) inline void __fastcall FreeBlock4ProxyOriginal(void* /*proxy*/, void*, void* /*block*/, int /*count*/)
    EI_ORIGINAL(0x00520F20)
__declspec(naked) inline void __fastcall DestroyModelMapOriginal(void* /*map*/) EI_ORIGINAL(0x00634160)
__declspec(naked) inline void __fastcall DestroyTextureMapOriginal(void* /*map*/) EI_ORIGINAL(0x00634200)
// 00625430 (rewritten): the trade panel's records again (plain RET).
__declspec(naked) inline void __fastcall TradePanelResetRecordsOriginal(void* /*panel*/) EI_ORIGINAL(0x00625430)
// 00616560 (rewritten as CMpHeroScreen::DrawVoices; plain RET).
__declspec(naked) inline void __fastcall HeroScreenDrawVoicesOriginal(void* /*screen*/) EI_ORIGINAL(0x00616560)
// The trade panel (rewritten in ui_trade_panel.cpp; through their address so the wrappers run; ECX, plain RET):
// 00625880 BuildLists, 006261A0 DrawLists, 00626D70 ClampScroll.
__declspec(naked) inline void __fastcall TradePanelBuildListsOriginal(void* /*panel*/) EI_ORIGINAL(0x00625880)
__declspec(naked) inline void __fastcall TradePanelDrawListsOriginal(void* /*panel*/) EI_ORIGINAL(0x006261A0)
__declspec(naked) inline void __fastcall TradePanelClampScrollOriginal(void* /*panel*/) EI_ORIGINAL(0x00626D70)
// 005F8070 (rewritten as CInterface3D::OnStringEvent; Rebirth's NewStringEvents patches it, so callers go through its
// address): a script string event on the interface (007C7D50; RET 4, no result).
__declspec(naked) inline void __fastcall InterfaceStringEventOriginal(void* /*interface*/, void*, const char* /*event*/)
    EI_ORIGINAL(0x005F8070)
// 004DEFA0 (the text manager; RET 8): the section names matching `pattern` ("pers *") into a CStringArray. 00609E70
// (a PersonTextMap; RET 0xC): insert with a hint (an STLport map<int, CString> node, a {key, CString} pair); the
// node's iterator is written to `result`, which is returned.
__declspec(naked) inline void** __fastcall PersonMapInsertHintOriginal(void* /*map*/, void*, void** /*result*/,
                                                                       void* /*hint*/, const void* /*pair*/)
    EI_ORIGINAL(0x00609E70)
// 00615D30 (thiscall on the multiplayer hero screen, RET 4; Rebirth-patched, the vanilla body is
// CMpHeroScreen::SetModeVanilla): the screen laid out for mode 9, 10 or 11.
__declspec(naked) inline void __fastcall HeroScreenSetModeOriginal(void* /*screen*/, void*, int /*mode*/) EI_ORIGINAL(0x00615D30)
// 0066ED30 (rewritten as CPlayerServer::SendPosition04; RET 0xC): message 0x04, a position and two dwords. 00592090
// (rewritten as CWorldServer::CompleteBriefing; RET 8): briefing variable `name` is complete for player `playerId`.
// 00607A60 (rewritten as CBriefingScreen::PlaySpeakerAnimation; RET 0xC).
__declspec(naked) inline void __fastcall WorldCompleteBriefingOriginal(void* /*world*/, void*, int /*playerId*/,
                                                                      const char* /*name*/) EI_ORIGINAL(0x00592090)
// A-I13. 0065CC00: the scrolling text's constructor (CUiScrollText; RET 0x1C, returns this): the interface, a string
// (the exe's empty text), the layout rectangle by value (left, top, right, bottom) and the sprites' depth (A-H20: the
// scroll bar's depth, not a line height). 005EACB0: the
// battle view's camera and pick input side (0x2C bytes, vtable 00740208; RET 4, returns this).
__declspec(naked) inline void* __fastcall ScrollTextConstructOriginal(void* /*memory*/, void*, CInterface3D*, const char* /*text*/,
                                                                      int /*left*/, int /*top*/, int /*right*/, int /*bottom*/,
                                                                      float /*depth*/) EI_ORIGINAL(0x0065CC00)

// A-I15. The client world (007AF5B8, thiscall, RET 4): 00565D00 the unit drawn at a screen point (the point's x, y; EAX
// the object or null); 00566100 selects `unit` (null: clears the selection; no result).
// The interface's shutdown (005F4F90) calls: 00444BD0 (thiscall) the debug information panel
// (0079B930, string_map.cpp) frees its surface and font; 00462DF0 writes config\keyboard.ini
// (key_bindings.cpp).
namespace strmap {
void __fastcall PanelRelease(void* /*panel*/);
}
// 00444C10 (thiscall, RET 4): the debug information panel's rectangle (string_map.cpp).
namespace strmap {
void __fastcall PanelSetRect(void* /*panel*/, void*, const tagRECT* /*rect*/);
}
// 004447E0 (the panel's slot 0, thiscall, RET 8; string_map.cpp's StringTableSet): panel[key] = value.
namespace strmap {
void __fastcall StringTableSet(void* /*panel*/, void*, const char* /*key*/,
                               const char* /*value*/);
}
// 00444CD0 (thiscall on the panel, plain RET; string_map.cpp's PanelTick): the panel redrawn every 500 ms and
// blitted.
namespace strmap {
void __fastcall PanelTick(void* /*panel*/);
}
// 004449A0 (the panel's slot 1, thiscall, RET 4; string_map.cpp's StringTableRemove): the line `key` removed.
namespace strmap {
void __fastcall StringTableRemove(void* /*panel*/, void*, const char* /*key*/);
}
namespace keybind {
void __cdecl SaveKeyBindings();
}
// The interface's Create (005F4990) calls: 00462C10 reads config\keyboard.ini (key_bindings.cpp); 00444A70
// (thiscall, RET 4; string_map.cpp's PanelInit) binds the debug information panel to the interface; 005F9810
// (thiscall on the help topics' map, RET 0xC; library) inserts a {CString, int} pair before `hint` and gives the
// new node in `out`, which it returns.
namespace keybind {
void __cdecl LoadKeyBindings();
}
namespace strmap {
void __fastcall PanelInit(void* /*panel*/, void*, CInterface3D* /*ui*/);
}
__declspec(naked) inline void** __fastcall HelpTopicsInsertOriginal(void* /*map*/, void*, void** /*out*/, void* /*hint*/,
                                                                    const void* /*pair*/) EI_ORIGINAL(0x005F9810)
// 005E3700 (thiscall on the chat overlay, plain RET; Rebirth-patched): the typed line drawn on its surface (hidden while
// the chat is closed), the caret clamped to it.
__declspec(naked) inline void __fastcall ChatOverlayDrawTypedOriginal(void* /*chat*/) EI_ORIGINAL(0x005E3700)
// 00661FE0 (thiscall on a CParty, plain RET): every item released (its slot 4), the item array's storage freed.
__declspec(naked) inline void __fastcall PartyReleaseItemsOriginal(void* /*party*/) EI_ORIGINAL(0x00661FE0)
// The game client's trade offer (ClientSession, thiscall on GameClient(); rewritten in net_client_*.cpp): 0041E180
// takes the acceptance back, 0041E3E0 drops the offer, 0041DA50 publishes our offer (plain RET each).
__declspec(naked) inline void __fastcall ClientCommitPartyOriginal(void* /*client*/) EI_ORIGINAL(0x0041E180)
__declspec(naked) inline void __fastcall ClientDropPartyOriginal(void* /*client*/) EI_ORIGINAL(0x0041E3E0)
__declspec(naked) inline void __fastcall ClientPublishPartyOriginal(void* /*client*/) EI_ORIGINAL(0x0041DA50)

// A-E2. 005DAEA0 (fastcall, RET 8; rewritten as SetUpCursors): the cursor (007C7BD8) initialised and given its shapes.
void __fastcall SetUpCursors(bool /*modeTwo*/, int /*context*/, int /*smallCursors*/, int /*flags*/);
// 005DAE70 (fastcall, RET 8; cursor_setup.cpp): the cursor shut down and set up again.
void __fastcall ResetCursors(bool /*modeTwo*/, int /*context*/, int /*smallCursors*/, int /*flags*/);

// C3m. Thiscall members of game objects, called through these views (cast the object's pointer).
// 005FE700 (rewritten as CUiWindow::Construct): the window base constructor (returns this). Base of the views.
struct WindowConstructOriginal {
    CUiWindow* Construct(CInterface3D* ui);
};
__declspec(naked) inline CUiWindow* WindowConstructOriginal::Construct(CInterface3D*) EI_ORIGINAL(0x005FE700)

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
// 005FD120 (rewritten as UiSurfaceRelease; plain RET): the CUiSurface's destructor body (releases its DC and surface).
__declspec(naked) inline void __fastcall SurfaceReleaseContentsOriginal(CUiSurface* /*surface*/) EI_ORIGINAL(0x005FD120)
// 005F8F10: the constructor of CBattleScreen's vector<int> instantiations from an allocator (thiscall, RET 4; no result).
__declspec(naked) inline void __fastcall IdVectorConstructOriginal(void* /*vector*/, void*, const void* /*allocator*/)
    EI_ORIGINAL(0x005F8F10)
