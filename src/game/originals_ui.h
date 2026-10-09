#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CInterface3D;
class CItem;
class CUiSurface;
namespace mfc { class CString; }
namespace ui_gdi { struct GameDC; }
struct ItemObjectCache;
struct tagRECT;
struct Vector3;
struct PersonTextNode;

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
// The edit field (CUiEditField, ui.h): 0065C010 constructor (RET 0x14: the interface and a RECT by value, passed here
// as its four ints; returns this), 0065C1A0 its look (RET 0x18: two ints, the font, an int, the text and caret
// colours), 0065C290 its text (RET 4).
__declspec(naked) inline void* __fastcall EditFieldConstructOriginal(void* /*memory*/, void*, CInterface3D*, int /*left*/,
                                                                     int /*top*/, int /*right*/, int /*bottom*/)
    EI_ORIGINAL(0x0065C010)
__declspec(naked) inline void __fastcall EditFieldSetLookOriginal(void* /*edit*/, void*, int, int, int /*font*/, int,
                                                                  std::uint32_t /*colour*/, std::uint32_t /*caret*/)
    EI_ORIGINAL(0x0065C1A0)
__declspec(naked) inline void __fastcall EditFieldSetTextOriginal(void* /*edit*/, void*, const void* /*text*/)
    EI_ORIGINAL(0x0065C290)
// 005E5200: the mouse cursor (007C7BD8) SetShape(shape), RET 4, returns the previous shape or -1.
__declspec(naked) inline int __fastcall CursorSetShapeOriginal(void* /*cursor*/, void*, int /*shape*/) EI_ORIGINAL(0x005E5200)
// 00634650: constructs a message box window in `memory` (RET 0x18, returns it).
__declspec(naked) inline void* __fastcall MessageBoxConstructOriginal(void* /*memory*/, void*, CInterface3D*,
                                                                      const char*, const char*, int, int, int)
    EI_ORIGINAL(0x00634650)
// 00620380: CUiItemStrip::Build(top) (RET 4; the flag is a byte; rewritten in ui_item_strip.cpp).
__declspec(naked) inline void __fastcall ItemStripBuildOriginal(void* /*strip*/, void*, int /*top*/) EI_ORIGINAL(0x00620380)
// 00458270 (__fastcall: ECX the section name, DL clears the screen first; then clear-after and two values on
// the stack, RET 0xC): plays the movies listed under a section (the titles' "<name>fin" / "<name>fout").
__declspec(naked) inline void __fastcall PlayMoviesOriginal(const char* /*section*/, int /*clearFirst*/, int, int, int)
    EI_ORIGINAL(0x00458270)
// 00630860: CGlobalMap::SelectZone(index, isLink) (RET 8; the flag is a byte; rewritten in ui_global_map_places.cpp).
__declspec(naked) inline void __fastcall GlobalMapSelectZoneOriginal(void* /*map*/, void*, int /*index*/, int /*isLink*/)
    EI_ORIGINAL(0x00630860)
// 00600270: CUiWindow's frame time in seconds, unrounded in ST0 (ECX = window, plain RET).
__declspec(naked) inline double __fastcall WindowFrameTimeOriginal(void* /*window*/) EI_ORIGINAL(0x00600270)
// 00578D10: sets the world server's flag through the game state at 007AFEA4. A free function: one stack argument,
// RET 4, ECX is not read (the callers' member `this` is the state but is ignored); EAX is not a result.
__declspec(naked) inline void __stdcall SetServerFlagOriginal(int /*flag*/) EI_ORIGINAL(0x00578D10)
// 00578D00: the flag's byte (007AFFCC) in AL; plain RET, ECX not read.
__declspec(naked) inline char __cdecl ServerFlagOriginal() EI_ORIGINAL(0x00578D00)
// 00458010 (__fastcall, two byte arguments in CL and DL; rewritten as FrameStep, app_frame.cpp): a frame step.
__declspec(naked) inline void __fastcall FrameStepOriginal(char, char) EI_ORIGINAL(0x00458010)

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
// 006B71C0: CSoundManager::StopAll(stopStream) (thiscall on the sound manager [007AF784], RET 4;
// rewritten in sound_manager.cpp; the name is historical).
__declspec(naked) inline void __fastcall LoadingStepOriginal(void* /*loading*/, void*, int /*step*/) EI_ORIGINAL(0x006B71C0)
// 005E6630 (rewritten as CursorSaveBackground, ui_cursor_frame.cpp; called through its address so the wrapper
// runs): the cursor saves the screen under its drawn rectangle into its background surface, from the primary
// (`fromPrimary`) or the render target (thiscall, RET 4).
__declspec(naked) inline void __fastcall CursorSaveBackgroundOriginal(void* /*cursor*/, void*, char /*fromPrimary*/)
    EI_ORIGINAL(0x005E6630)

// C3j. 005E7060: sprite SetScale (RET 4; the float is only copied to three fields, so it is raw dword bits; no result).
__declspec(naked) inline void __fastcall SpriteSetScaleOriginal(void* /*sprite*/, void*, std::uint32_t /*scaleBits*/)
    EI_ORIGINAL(0x005E7060)
// 005E6D90: a sprite (CUiMesh) draws itself (thiscall, RET 4: the draw pass; no result).
__declspec(naked) inline void __fastcall SpriteDrawOriginal(void* /*sprite*/, void*, int /*pass*/) EI_ORIGINAL(0x005E6D90)
// 005E6FA0: sprite Place(x, y, z) (RET 0xC, no result).
__declspec(naked) inline void __fastcall SpritePlaceOriginal(void* /*sprite*/, void*, float, float, float) EI_ORIGINAL(0x005E6FA0)
// 006010E0: CUiContainer updates its children through slot 24 (plain RET, no result).
__declspec(naked) inline void __fastcall UiUpdateChildrenOriginal(void* /*container*/) EI_ORIGINAL(0x006010E0)
// 0063B920 (rewritten as CBattleScreenSetup::UnitName; called through its address so the wrapper runs): the
// battle screen writes a unit's name into a CString (RET 8: unit, out; no result).
__declspec(naked) inline void __fastcall BattleShowPanelUnitOriginal(void* /*screen*/, void*, void* /*unit*/, void* /*extra*/)
    EI_ORIGINAL(0x0063B920)
// 0063CD20 / 0063E750 / 0063F730: quick, spell and action panel slot updates (RET 8: slot, flag; no result).
__declspec(naked) inline void __fastcall QuickPanelSetOriginal(void* /*panel*/, void*, int, int) EI_ORIGINAL(0x0063CD20)
__declspec(naked) inline void __fastcall SpellPanelSetOriginal(void* /*panel*/, void*, int, int) EI_ORIGINAL(0x0063E750)
__declspec(naked) inline void __fastcall ActionPanelSetOriginal(void* /*panel*/, void*, int, int) EI_ORIGINAL(0x0063F730)
// 0063F7A0: the action panel unhighlights every button, then +0x150 = -1 (ECX = panel, plain RET, no result;
// 0063E7C0 is the spell panel's copy).
__declspec(naked) inline void __fastcall ActionPanelClearOriginal(void* /*panel*/) EI_ORIGINAL(0x0063F7A0)
// 0065CF30: appends a line to a log window (RET 4, no result).
__declspec(naked) inline void __fastcall LogAddTextOriginal(void* /*window*/, void*, const char* /*text*/) EI_ORIGINAL(0x0065CF30)
// 0065CFE0: replaces a log window's text (a CUiScrollText: tokens freed, `text` tokenised, the
// layout redone, scrolled to the top; RET 4, no result).
__declspec(naked) inline void __fastcall LogSetTextOriginal(void* /*window*/, void*, const char* /*text*/) EI_ORIGINAL(0x0065CFE0)
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

// 00623880 (rewritten as CHeroStatsPanel::DrawStats; called through its address so the wrapper runs): the
// statistics column draws itself (ECX = panel, plain RET, no result).
__declspec(naked) inline void __fastcall StatsPanelDrawStatsOriginal(void* /*panel*/) EI_ORIGINAL(0x00623880)

// 0065E320: CUiTooltip constructor in `memory` (0x184 bytes; thiscall, RET 0x14: interface, region, text, flag, extra; the
// text is split into lines, not kept); returns `memory`. 0065E540: its SetText (RET 4, no result).
__declspec(naked) inline struct CUiTooltip* __fastcall TooltipConstructOriginal(void* /*memory*/, void*, CInterface3D*,
                                                                              class CUiRegion*, const char* /*text*/,
                                                                              int /*flag*/, int /*extra*/)
    EI_ORIGINAL(0x0065E320)
__declspec(naked) inline void __fastcall TooltipSetTextOriginal(struct CUiTooltip* /*tooltip*/, void*, const char* /*text*/)
    EI_ORIGINAL(0x0065E540)

// 005E44A0 (Rebirth-patched by ImproveConsole; rewritten as ConsoleWindow::OnChar): the console's typed character
// (thiscall, RET 0xC: key, repeat, flags; no result). 005E47E0 (rewritten as ConsoleWindow::OnKeyUp; through its
// address so the wrapper runs): the console's key release (RET 0xC).
__declspec(naked) inline void __fastcall ConsoleOnCharOriginal(void* /*console*/, void*, int, int, int) EI_ORIGINAL(0x005E44A0)
__declspec(naked) inline void __fastcall ConsoleOnKeyUpOriginal(void* /*console*/, void*, int, int, int) EI_ORIGINAL(0x005E47E0)

// A-I4. 005F7350 (rewritten as CInterface3D::PushWindow; through its address so the wrapper runs): opens `window` on top
// (thiscall, RET 0xC; no result). 005F51F0 (rewritten as CInterface3D::ResetSavedState): clears the saved state
// (ECX, plain RET). 00600CC0 (rewritten as CUiContainer::AddInput): appends an extra input side (RET 4).
__declspec(naked) inline void __fastcall InterfacePushWindowOriginal(CInterface3D*, void*, void* /*window*/, int, int)
    EI_ORIGINAL(0x005F7350)
__declspec(naked) inline void __fastcall InterfaceResetSavedStateOriginal(CInterface3D*) EI_ORIGINAL(0x005F51F0)
// 005F8340 (Rebirth-patched; thiscall, RET 0xC): the extent (a SIZE written to `out`, returned) of a text in interface
// font `font`. The text is a CString taken by value: its data pointer is handed over and the callee releases it.
__declspec(naked) inline void* __fastcall InterfaceTextExtentOriginal(CInterface3D*, void*, void* /*out SIZE*/,
                                                                      char* /*text, owned*/, int /*font*/)
    EI_ORIGINAL(0x005F8340)
__declspec(naked) inline void __fastcall ContainerAddInputOriginal(void* /*container*/, void*, void* /*input*/)
    EI_ORIGINAL(0x00600CC0)
// 0062AC90 (rewritten as CUiRegion::SetRectFrom): the region's SetRect from a RECT passed by value (RET 0x10).
__declspec(naked) inline void __fastcall RegionSetRectFromOriginal(class CUiRegion*, void*, int /*left*/, int /*top*/,
                                                                   int /*right*/, int /*bottom*/) EI_ORIGINAL(0x0062AC90)
// 005FCDD0: CUiSurface constructor in `memory` over the layout rectangle `rect` with format `format` (thiscall, RET 0xC,
// returns `memory`; string_map.cpp and ui_console_window.cpp still have their own forwarders).
__declspec(naked) inline CUiSurface* __fastcall SurfaceConstructOnRectOriginal(void* /*memory*/, void*,
                                                                                    CInterface3D* /*ui*/,
                                                                                    const tagRECT* /*rect*/,
                                                                                    int /*format*/) EI_ORIGINAL(0x005FCDD0)
// 005FE2F0: CUiSurface fills its DC's surface with black (ECX, plain RET).
__declspec(naked) inline void __fastcall SurfaceClearOriginal(void* /*surface*/) EI_ORIGINAL(0x005FE2F0)
// The client glue objects (007AFEA4 / 007AFEA5 / 007AFEA6, thiscall; the object is not read): 00578DC0 the game's
// folder (a CString*); 00578950 marks the interface changed (the window list +0x94 flag 8); 005786A0 the weather
// (kind 0 clear / 1 rain, strength; RET 8); 005786E0 the world's time of day, unrounded in ST0; 00578960 (RET 4) asks
// the sound manager for a music stream (null: the default 0079BECC); 00578880 (RET 4) plays a camera path file.
__declspec(naked) inline const mfc::CString* __fastcall GameDirectoryOriginal(void*) EI_ORIGINAL(0x00578DC0)
__declspec(naked) inline void __fastcall GameMarkInterfaceChangedOriginal(void*) EI_ORIGINAL(0x00578950)
__declspec(naked) inline void __fastcall GameSetWeatherOriginal(void*, void*, int /*kind*/, float /*strength*/)
    EI_ORIGINAL(0x005786A0)
__declspec(naked) inline double __fastcall GameTimeOfDayOriginal(void*) EI_ORIGINAL(0x005786E0)
__declspec(naked) inline void __fastcall GamePlayMusicOriginal(void*, void*, const char* /*stream*/) EI_ORIGINAL(0x00578960)
__declspec(naked) inline void __fastcall GamePlayCameraPathOriginal(void*, void*, const char* /*file*/) EI_ORIGINAL(0x00578880)
// 006B51F0 (rewritten as CMusicSystem::PlayMainMenu): the music system [007AF760] plays the main menu's music.
__declspec(naked) inline void __fastcall MusicPlayMainMenuOriginal(void* /*music*/) EI_ORIGINAL(0x006B51F0)
// 005187B0 (thiscall on a figure, RET 8): plays `animation` (`restart` 1: from the start). 00512B20 (thiscall on the object
// manager 007AF5F8, RET 8): its object of `kind` named `name` (an animation for kind 0x80590000, the rest pose), or null.
__declspec(naked) inline void __fastcall FigurePlayAnimationOriginal(void* /*figure*/, void*, void* /*animation*/,
                                                                     int /*restart*/) EI_ORIGINAL(0x005187B0)
__declspec(naked) inline void* __fastcall ManagerFindNamedOriginal(void* /*manager*/, void*, const mfc::CString* /*name*/,
                                                                   std::uint32_t /*kind*/) EI_ORIGINAL(0x00512B20)

// A-I5. The texture manager [007AF744] (thiscall): 00691D00 its texture of `kind` named `name`, found or made (RET 8);
// 006922A0 / 006926F0 (rewritten in texture.cpp; through their address so the wrappers run) load it, from the archives or
// from `directory` (RET 4 / 8; nonzero on success); 00691D80 (rewritten as CTextureManager::NewTexture) a new CPU-filled
// texture (width, height, kind; RET 0xC).
__declspec(naked) inline void* __fastcall TextureNamedOriginal(void* /*manager*/, void*, int /*kind*/, const char* /*name*/)
    EI_ORIGINAL(0x00691D00)
__declspec(naked) inline int __fastcall TextureLoadOriginal(void* /*manager*/, void*, void* /*texture*/) EI_ORIGINAL(0x006922A0)
__declspec(naked) inline int __fastcall TextureLoadFromOriginal(void* /*manager*/, void*, void* /*texture*/,
                                                                const char* /*directory*/) EI_ORIGINAL(0x006926F0)
__declspec(naked) inline std::uint8_t* __fastcall NewManagedTextureOriginal(void* /*manager*/, void*, int /*width*/,
                                                                            int /*height*/, int /*kind*/)
    EI_ORIGINAL(0x00691D80)
// 005FD7A0 (rewritten as CUiSurface::SwapContents): keeps a surface's contents in memory (0) or restores them (1) (RET 4).
// 006002C0 (rewritten as CUiWindow::ShowSurfaces): a window's slot 19 for its surfaces (RET 4).
__declspec(naked) inline void __fastcall SurfaceSwapContentsOriginal(void* /*surface*/, void*, int /*restore*/)
    EI_ORIGINAL(0x005FD7A0)
__declspec(naked) inline void __fastcall WindowShowSurfacesOriginal(void* /*window*/, void*, int /*show*/)
    EI_ORIGINAL(0x006002C0)

// A-I6. 005E67B0: the cursor (007C7BD8) is held in place while the right button turns the view (thiscall, RET 4: a
// byte into +0x90 under its lock when the cursor exists; no result).
__declspec(naked) inline void __fastcall CursorSetHeldOriginal(void* /*cursor*/, void*, char /*held*/) EI_ORIGINAL(0x005E67B0)
// 005A6C70 (a jump to 0059F210): the figure's Create (vtable slot 2; thiscall, plain RET; the result is not used).
__declspec(naked) inline void __fastcall FigureCreateOriginal(void* /*figure*/) EI_ORIGINAL(0x005A6C70)
// 00708D79: MFC's CDC::FromHandle (static, stdcall): the temporary CDC of `dc`.
__declspec(naked) inline void* __stdcall CdcFromHandleOriginal(void* /*dc*/) EI_ORIGINAL(0x00708D79)
// 0065ECB0: the DirectInput mouse's state read (thiscall, RET 4; the argument is not read; returns the DIMOUSESTATE
// inside the object).
__declspec(naked) inline const long* __fastcall DirectInputMousePollOriginal(void* /*mouse*/, void*, int /*unused*/)
    EI_ORIGINAL(0x0065ECB0)
// 007022A0: MFC's AfxBeginThread(procedure, parameter, priority, stack size, flags, security) (__stdcall; returns the
// CWinThread).
__declspec(naked) inline void* __stdcall AfxBeginThreadOriginal(void* /*procedure*/, void* /*parameter*/, int /*priority*/,
                                                                unsigned /*stack*/, unsigned long /*flags*/,
                                                                void* /*security*/) EI_ORIGINAL(0x007022A0)
// 00637500 / 00637750 (rewritten as CBattleScreenSetup::UseOnUnit / UseAtPoint; through their addresses so the
// wrappers run): the pending spell or quick item used on `target` (thiscall, RET 0xC: target, quick (low byte), ctrl)
// or at the point (RET 0x14: x, y, z, unit, quick (low byte)); no result.
__declspec(naked) inline void __fastcall BattleUseOnUnitOriginal(void* /*screen*/, void*, void* /*target*/, char /*quick*/,
                                                                 int /*ctrl*/) EI_ORIGINAL(0x00637500)
__declspec(naked) inline void __fastcall BattleUseAtPointOriginal(void* /*screen*/, void*, float /*x*/, float /*y*/, float /*z*/,
                                                                  void* /*unit*/, char /*quick*/) EI_ORIGINAL(0x00637750)
// 0068B770 (rewritten as CSpell::IsOffensive; Rebirth's FixSpellSelfCasting patches it): an attack spell (thiscall on
// the spell, plain RET, AL).
__declspec(naked) inline bool __fastcall SpellIsOffensiveOriginal(void* /*spell*/) EI_ORIGINAL(0x0068B770)
// 005E4EB0 (rewritten as CursorImage::Construct; through its address so the wrapper runs): a cursor image on
// `memory` from the picture file `name` (thiscall, RET 0x10: name, hot spot x, y, flag; returns `memory`).
__declspec(naked) inline struct CursorImage* __fastcall CursorImageConstructOriginal(void* /*memory*/, void*, const char* /*name*/,
                                                                                    int /*hotX*/, int /*hotY*/, int /*flag*/)
    EI_ORIGINAL(0x005E4EB0)
// 006570E0: saves the game under `name` (ECX; DL the autosave flag; plain RET, no result).
__declspec(naked) inline void __fastcall AutoSaveOriginal(const char* /*name*/, int /*flag*/) EI_ORIGINAL(0x006570E0)
// 0040D3B0 (rewritten as CCamera::SetTarget; through its address so the wrapper runs): the camera follows `target`
// (thiscall on 0079B2A8, RET 4).
__declspec(naked) inline void __fastcall CameraSetTargetOriginal(void* /*camera*/, void*, const void* /*target*/)
    EI_ORIGINAL(0x0040D3B0)
// 005EBDB0: the battle screen's party array (an MFC CArray of unit pointers) SetSize (RET 8; growBy -1 keeps the step).
__declspec(naked) inline void __fastcall PartyArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x005EBDB0)
// 00637DC0 (rewritten as CBattleScreenSetup::RefreshSelection; through its address so the wrapper runs; ECX, plain RET).
__declspec(naked) inline void __fastcall BattleRefreshSelectionOriginal(void* /*screen*/) EI_ORIGINAL(0x00637DC0)
// 006407E0: the portraits rebuild their slots from the battle screen's party (RET 4: camera player first; no result).
__declspec(naked) inline void __fastcall PortraitsRebuildOriginal(void* /*portraits*/, void*, int /*cameraFirst*/)
    EI_ORIGINAL(0x006407E0)
// The top panel (thiscall): 0064A4E0 adds the "combat_picked_up" (or with `crippled` "combat_crippled") line of an item
// (RET 8); 0064A630 the picked-up money line, for money > 0 (RET 4); 0064A7A0 shows its current page (plain RET).
__declspec(naked) inline void __fastcall TopPanelPickedUpOriginal(void* /*panel*/, void*, CItem* /*item*/, char /*crippled*/)
    EI_ORIGINAL(0x0064A4E0)
__declspec(naked) inline void __fastcall TopPanelMoneyOriginal(void* /*panel*/, void*, int /*money*/) EI_ORIGINAL(0x0064A630)
__declspec(naked) inline void __fastcall TopPanelShowPageOriginal(void* /*panel*/) EI_ORIGINAL(0x0064A7A0)
// 006575B0: loads the saved game `name` (ECX; plain RET; not on a network client).
__declspec(naked) inline void __fastcall LoadSavedGameOriginal(const char* /*name*/) EI_ORIGINAL(0x006575B0)
// 005F7E90: the interface stops sound `id` (thiscall, the interface not read; -1 none; RET 4).
__declspec(naked) inline void __fastcall InterfaceStopSoundOriginal(CInterface3D*, void*, int /*id*/) EI_ORIGINAL(0x005F7E90)
// The camp (CCampPages, thiscall on the window): 0060E9E0 (rewritten as ShowPage; through its address so the wrapper runs)
// shows a page (RET 4); 00611930 (rewritten as UpdateAction, likewise) works out the page's action (plain RET); 00611220 /
// 00611760 carry out the first / second action button's deal (plain RET).
__declspec(naked) inline void __fastcall CampShowPageOriginal(void* /*camp*/, void*, int /*page*/) EI_ORIGINAL(0x0060E9E0)
__declspec(naked) inline void __fastcall CampUpdateActionOriginal(void* /*camp*/) EI_ORIGINAL(0x00611930)
__declspec(naked) inline void __fastcall CampDoActionOriginal(void* /*camp*/) EI_ORIGINAL(0x00611220)
__declspec(naked) inline void __fastcall CampDoSecondActionOriginal(void* /*camp*/) EI_ORIGINAL(0x00611760)
// 00621570 (rewritten as CUiItemStrip::ScrollToItem; through its address so the wrapper runs; RET 4).
__declspec(naked) inline void __fastcall ItemStripScrollToItemOriginal(void* /*strip*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00621570)
// 005F7C80 / 005F7C60: a multiplayer client's chat line takes Backspace / Enter (ECX = the interface; plain RET).
__declspec(naked) inline void __fastcall ChatKeyBackspaceOriginal(CInterface3D*) EI_ORIGINAL(0x005F7C80)
__declspec(naked) inline void __fastcall ChatKeyEnterOriginal(CInterface3D*) EI_ORIGINAL(0x005F7C60)
// 00566030: the client world (007AF5B8) controls `unit` (thiscall, RET 4; no result).
__declspec(naked) inline void __fastcall ClientWorldControlUnitOriginal(void* /*world*/, void*, void* /*unit*/)
    EI_ORIGINAL(0x00566030)
// 0057ECA0: the game (007AFE20) drops its network client and server sessions (thiscall, plain RET).
__declspec(naked) inline void __fastcall GameDisconnectOriginal(void* /*game*/) EI_ORIGINAL(0x0057ECA0)
// 00578AC0: the client glue (007AFEA5, not read) leaves the zone through exit `exit` (a network client asks its
// session; otherwise the local view, 0066B140; RET 4).
__declspec(naked) inline void __fastcall GameLeaveThroughExitOriginal(void* /*glue*/, void*, int /*exit*/)
    EI_ORIGINAL(0x00578AC0)

// A-I7. The description panel (CItemInfoPanel, thiscall; rewritten in ui_item_info.cpp, called through their address so
// the wrappers run): 005EE6C0 Describe(item), 005F2910 ShowHelp(topic), 005F1590 DescribeStats(info) (each RET 4).
__declspec(naked) inline void __fastcall InfoPanelDescribeOriginal(void* /*panel*/, void*, void* /*item*/)
    EI_ORIGINAL(0x005EE6C0)
__declspec(naked) inline void __fastcall InfoPanelShowHelpOriginal(void* /*panel*/, void*, int /*topic*/)
    EI_ORIGINAL(0x005F2910)
__declspec(naked) inline void __fastcall InfoPanelDescribeStatsOriginal(void* /*panel*/, void*, void* /*info*/)
    EI_ORIGINAL(0x005F1590)
// 00625690 / 00624CB0 (rewritten as CTradePanel::HoverInfo / CHeroStatsPanel::HoverInfo): what the statistics under
// (x, y) are (RET 0xC). 00625530 / 00625560: the trade panel's experience to spend / the cost of what was bought since the
// panel's record was made (plain RET, EAX).
__declspec(naked) inline void __fastcall TradePanelHoverInfoOriginal(void* /*panel*/, void*, int, int, void* /*info*/)
    EI_ORIGINAL(0x00625690)
__declspec(naked) inline void __fastcall StatsPanelHoverInfoOriginal(void* /*panel*/, void*, int, int, void* /*info*/)
    EI_ORIGINAL(0x00624CB0)
__declspec(naked) inline int __fastcall TradePanelExperienceOriginal(void* /*panel*/) EI_ORIGINAL(0x00625530)
__declspec(naked) inline int __fastcall TradePanelPurchaseCostOriginal(void* /*panel*/) EI_ORIGINAL(0x00625560)
// 005EC300 (rewritten as CUiWindow::DrawHeroPreview): the hero preview (RET 0x14: pass, model, turn, depth, wide).
__declspec(naked) inline void __fastcall WindowDrawHeroPreviewOriginal(void* /*window*/, void*, int /*pass*/,
                                                                       void* /*model*/, float /*turn*/,
                                                                       float /*depth*/, int /*wide*/)
    EI_ORIGINAL(0x005EC300)
// 0065B090 (rewritten as CUiScrollbar::TakePosition): the position, 1 when it moved since the last call (RET 4).
__declspec(naked) inline int __fastcall ScrollbarTakePositionOriginal(void* /*scrollbar*/, void*, int* /*position*/)
    EI_ORIGINAL(0x0065B090)
// The multiplayer hero screen (thiscall, plain RET): 006167C0 draws the hero list, 00616560 the voice list.
__declspec(naked) inline void __fastcall HeroScreenDrawListOriginal(void* /*screen*/) EI_ORIGINAL(0x006167C0)
// 005789E0: the client glue (007AFEA5, not read) hands a record array (CArray of unit records) to the client world
// (00567400 on 007AF5B8; RET 4). 006632B0: the multiplayer heroes (007C82A0) save themselves (plain RET).
__declspec(naked) inline void __fastcall GameTakeUnitRecordsOriginal(void* /*glue*/, void*, void* /*records*/)
    EI_ORIGINAL(0x005789E0)
__declspec(naked) inline void __fastcall MpHeroesSaveOriginal(void* /*heroes*/) EI_ORIGINAL(0x006632B0)
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
__declspec(naked) inline CItem* __fastcall ItemScreenReturnAllOriginal(void* /*screen*/, void*, void* /*to*/, void* /*from*/)
    EI_ORIGINAL(0x005EE2C0)
__declspec(naked) inline CItem* __fastcall ItemScreenMoveItemOriginal(void* /*screen*/, void*, CItem* /*item*/, void* /*to*/,
                                                                      void* /*from*/, int /*limit*/, int /*merge*/,
                                                                      int /*turning*/) EI_ORIGINAL(0x005EE1B0)
// 006255A0: the trade panel settles its deal (thiscall, RET 8: two flags read as bytes; EAX a result).
__declspec(naked) inline int __fastcall TradePanelSettleOriginal(void* /*panel*/, void*, int /*a*/, int /*all*/)
    EI_ORIGINAL(0x006255A0)
// STLport containers' storage (library code): 006344C0 / 00520F60 give `count` entries of 8 / 4 bytes back to the node
// pool (__fastcall: block, count); 00520F20 the same for 4-byte entries through an allocator proxy (thiscall, RET 8: block,
// count). 00634160 / 00634200: the hero screen's model / texture map destroyed (nodes erased, header freed; ECX).
__declspec(naked) inline void __fastcall FreeBlock8Original(void* /*block*/, int /*count*/) EI_ORIGINAL(0x006344C0)
__declspec(naked) inline void __fastcall FreeBlock4Original(void* /*block*/, int /*count*/) EI_ORIGINAL(0x00520F60)
__declspec(naked) inline void __fastcall FreeBlock4ProxyOriginal(void* /*proxy*/, void*, void* /*block*/, int /*count*/)
    EI_ORIGINAL(0x00520F20)
__declspec(naked) inline void __fastcall DestroyModelMapOriginal(void* /*map*/) EI_ORIGINAL(0x00634160)
__declspec(naked) inline void __fastcall DestroyTextureMapOriginal(void* /*map*/) EI_ORIGINAL(0x00634200)
// 00578980: the client glue (007AFEA5, not read) finds the unit with id `id` in the client world (0055FB00 on 007AF5B8;
// RET 4; EAX the unit or null). 00662F80: the multiplayer heroes (007C82A0) make `hero` the current one (RET 4).
__declspec(naked) inline void* __fastcall GameUnitByIdOriginal(void* /*glue*/, void*, std::uint32_t /*id*/)
    EI_ORIGINAL(0x00578980)
__declspec(naked) inline void __fastcall MpHeroesSelectOriginal(void* /*heroes*/, void*, void* /*hero*/) EI_ORIGINAL(0x00662F80)
// The hero panels by their address (rewritten; the wrappers run): 006232D0 the statistics panel's hero (index, name;
// RET 8), 00625430 the trade panel's records again (plain RET), 00625860 its hero (RET 4).
__declspec(naked) inline void __fastcall StatsPanelSetHeroOriginal(void* /*panel*/, void*, int /*index*/, const char* /*name*/)
    EI_ORIGINAL(0x006232D0)
__declspec(naked) inline void __fastcall TradePanelResetRecordsOriginal(void* /*panel*/) EI_ORIGINAL(0x00625430)
__declspec(naked) inline void __fastcall TradePanelSelectHeroOriginal(void* /*panel*/, void*, int /*index*/)
    EI_ORIGINAL(0x00625860)
// 0065AFE0 (RET 4): a scroll bar's range 0..maximum (negative: 0); the arrows' sprites shown when it can scroll, the
// position set again (slot 25).
__declspec(naked) inline void __fastcall ScrollbarSetRangeOriginal(void* /*scrollbar*/, void*, int /*maximum*/)
    EI_ORIGINAL(0x0065AFE0)
// 005789C0: the client glue (007AFEA5, not read) hands a record array to the client world with a flag (00566F70 on
// 007AF5B8; RET 8).
__declspec(naked) inline void __fastcall GameTakeUnitRecordsFlagOriginal(void* /*glue*/, void*, void* /*records*/, int /*flag*/)
    EI_ORIGINAL(0x005789C0)
// 00616E20 (rewritten as CMpHeroScreen::MakePreview; thiscall, RET 4): the preview's unit made for the local player's
// first record (through its address so the wrapper runs).
__declspec(naked) inline void __fastcall HeroScreenMakePreviewOriginal(void* /*screen*/, void*, int /*flag*/)
    EI_ORIGINAL(0x00616E20)
// 00616910 / 00616560 (rewritten as CMpHeroScreen::ShowHero (RET 4) / DrawVoices; plain RET).
__declspec(naked) inline void __fastcall HeroScreenShowHeroOriginal(void* /*screen*/, void*, int /*index*/)
    EI_ORIGINAL(0x00616910)
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
// A-I10. The briefing / base screen (CBriefingScreen, ui_briefing.h; thiscall on the window): 006064E0 starts dialogue topic_
// (RET 8: a flag, a second flag); 00604C50 the next phrase (RET 4: `skip`, the phrase's voice stopped); 00608450 the rewards
// taken (plain RET); 00608B60 (rewritten as OpenTopics; through its address so the wrapper runs) a person's topics (RET 4);
// 006071E0 draws the topic list (plain RET); 00607360 fills topics_ with a person's topics (RET 4: the person's id).
__declspec(naked) inline void __fastcall BriefingStartDialogueOriginal(void* /*screen*/, void*, int, int) EI_ORIGINAL(0x006064E0)
__declspec(naked) inline void __fastcall BriefingNextPhraseOriginal(void* /*screen*/, void*, int /*skip*/) EI_ORIGINAL(0x00604C50)
__declspec(naked) inline void __fastcall BriefingTakeRewardsOriginal(void* /*screen*/) EI_ORIGINAL(0x00608450)
__declspec(naked) inline void __fastcall BriefingOpenTopicsOriginal(void* /*screen*/, void*, void* /*person*/)
    EI_ORIGINAL(0x00608B60)
__declspec(naked) inline void __fastcall BriefingDrawTopicsOriginal(void* /*screen*/) EI_ORIGINAL(0x006071E0)
__declspec(naked) inline void __fastcall BriefingFindTopicsOriginal(void* /*screen*/, void*, int /*personId*/)
    EI_ORIGINAL(0x00607360)
// A-I11. 0066F840 (CPlayerServer, RET 8): message 0x19, a dialogue event (byte `what`: 3 the phrases start, 2 a
// person leaves the dialogue) and a dword (a unit id, or 0). 006B4E10 (on the music system [007AF760], ECX not read):
// the sound manager's stream fades out (006BAFA0).
__declspec(naked) inline void __fastcall PlayerSendDialogueEventOriginal(void* /*server*/, void*, int /*what*/, int /*value*/)
    EI_ORIGINAL(0x0066F840)
__declspec(naked) inline void __fastcall MusicFadeOutOriginal(void* /*music*/) EI_ORIGINAL(0x006B4E10)
// 004DEFA0 (the text manager; RET 8): the section names matching `pattern` ("pers *") into a CStringArray. 00609E70
// (a PersonTextMap; RET 0xC): insert with a hint (an STLport map<int, CString> node, a {key, CString} pair); the
// node's iterator is written to `result`, which is returned.
__declspec(naked) inline void __fastcall TextsFindKeysOriginal(void* /*texts*/, void*, void* /*out CStringArray*/,
                                                               const char* /*pattern*/) EI_ORIGINAL(0x004DEFA0)
__declspec(naked) inline PersonTextNode** __fastcall PersonMapInsertHintOriginal(void* /*map*/, void*, PersonTextNode** /*result*/,
                                                                                 PersonTextNode* /*hint*/, const void* /*pair*/)
    EI_ORIGINAL(0x00609E70)
// 00578760 (the client glue 007AFEA6, not read; RET 8): the server unit with id `id` is moved to height `z` (its
// sector map entry updated, part 2 marked) unless it is there already. 006089D0 (the briefing screen; RET 0x28): `unit`
// walks to a place (x, y, z) and turns to a direction (x, y, z); `waited` (byte) makes the dialogue wait for it,
// `immediate` (byte) places it at once instead, `time` the walk's time argument.
__declspec(naked) inline void __fastcall GameSetUnitHeightOriginal(void* /*glue*/, void*, int /*id*/, float /*z*/)
    EI_ORIGINAL(0x00578760)
__declspec(naked) inline void __fastcall BriefingMovePersonOriginal(void* /*screen*/, void*, void* /*unit*/, float, float, float,
                                                                    float, float, float, int /*waited*/, int /*immediate*/,
                                                                    int /*time*/) EI_ORIGINAL(0x006089D0)
// A-I12. 00608210 (rewritten as CBriefingScreen::StartPhrases; through its address so the wrapper runs; plain RET).
// 00578560 (the client glue 007AFEA6, not read; RET 4): the local player's "constr.current" = `constructor` (the
// constructor the camp opens with; message to the server). 00578B00 (the client glue, not read; RET 4): the client
// asks the server for interface screen `id` (1 battle, 3 briefing, 4 global map, 5 camp).
__declspec(naked) inline void __fastcall BriefingStartPhrasesOriginal(void* /*screen*/) EI_ORIGINAL(0x00608210)
__declspec(naked) inline void __fastcall GameSetConstructorOriginal(void* /*glue*/, void*, float /*constructor*/)
    EI_ORIGINAL(0x00578560)
__declspec(naked) inline void __fastcall GameRequestScreenOriginal(void* /*glue*/, void*, int /*id*/) EI_ORIGINAL(0x00578B00)
// 006B4E20 (on the music system [007AF760], ECX not read): the sound manager's stream fades in (006BAFE0). 0066ED30
// (rewritten as CPlayerServer::SendPosition04; through its address so the wrapper runs; RET 0xC): message 0x04, a
// position and two dwords. 00592090 (rewritten as CWorldServer::CompleteBriefing; through its address so the wrapper
// runs; RET 8): briefing variable `name` is complete for player `playerId`.
// 00605DE0 (the briefing screen; RET 0xC): "#show": a dialogue item `name` shown (index, flag). Through their address so
// the wrappers run: 00607A60 PlaySpeakerAnimation (RET 0xC), 00607EC0 ViewPreset (RET 4), 006062C0 HideSpeaker (RET 0xC).
__declspec(naked) inline void __fastcall BriefingShowItemOriginal(void* /*screen*/, void*, const char* /*name*/, int /*index*/,
                                                                  int /*flag*/) EI_ORIGINAL(0x00605DE0)
__declspec(naked) inline void __fastcall BriefingSpeakerAnimationOriginal(void* /*screen*/, void*, void* /*unit*/, int /*talking*/,
                                                                          int /*third*/) EI_ORIGINAL(0x00607A60)
__declspec(naked) inline void __fastcall BriefingViewPresetOriginal(void* /*screen*/, void*, int /*preset*/) EI_ORIGINAL(0x00607EC0)
__declspec(naked) inline void __fastcall BriefingHideSpeakerOriginal(void* /*screen*/, void*, const char* /*name*/, int /*immediate*/,
                                                                     int /*time*/) EI_ORIGINAL(0x006062C0)
__declspec(naked) inline void __fastcall MusicFadeInOriginal(void* /*music*/) EI_ORIGINAL(0x006B4E20)
__declspec(naked) inline int __fastcall PlayerSendPosition04Original(void* /*server*/, void*, const Vector3* /*at*/,
                                                                     std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0066ED30)
__declspec(naked) inline void __fastcall WorldCompleteBriefingOriginal(void* /*world*/, void*, int /*playerId*/,
                                                                      const char* /*name*/) EI_ORIGINAL(0x00592090)
// 00607BB0 (rewritten as CBriefingScreen::ViewSpeaker; through its address so the wrapper runs; RET 0x10).
__declspec(naked) inline void __fastcall BriefingViewSpeakerOriginal(void* /*screen*/, void*, void* /*unit*/, float /*angle*/,
                                                                     float /*distance*/, float /*height*/) EI_ORIGINAL(0x00607BB0)
// 0066EDA0 (rewritten as CPlayerServer::SendPosition0F; RET 0xC): message 0x0F, a camera position `at`, the point it
// looks at and a flag byte.
__declspec(naked) inline void __fastcall PlayerSendViewOriginal(void* /*server*/, void*, const Vector3* /*at*/,
                                                                const Vector3* /*point*/, int /*flag*/) EI_ORIGINAL(0x0066EDA0)
// 00578C40: the client glue (007AFEA4, not read) copies the local player's variables whose names start with `prefix`
// into `out` (a NamedFloatTable; emptied when there is no player; RET 8).
__declspec(naked) inline void __fastcall GameCopyVariablesOriginal(void* /*glue*/, void*, const char* /*prefix*/,
                                                                   void* /*out*/) EI_ORIGINAL(0x00578C40)
// 0063B5D0 (rewritten as CObjectivesWindow::Construct; through its address so the wrapper runs; RET 4, returns this).
// 0064BE40: the objectives window's setup before it opens (thiscall, RET 0x10): zoneName_ = zone, exitDistances_ = the
// second, twoButtons_ = the third, listZones_ = the fourth argument.
__declspec(naked) inline void* __fastcall ObjectivesConstructOriginal(void* /*memory*/, void*, CInterface3D*)
    EI_ORIGINAL(0x0063B5D0)
__declspec(naked) inline void __fastcall ObjectivesSetupOriginal(void* /*window*/, void*, const char* /*zone*/, void* /*distances*/,
                                                                 int /*twoButtons*/, int /*listZones*/) EI_ORIGINAL(0x0064BE40)
// A-I13. 0065CC00: the scrolling text's constructor (CUiScrollText; RET 0x1C, returns this): the interface, a string
// (the exe's empty text), the layout rectangle by value (left, top, right, bottom) and the lines' height. 005EACB0: the
// battle view's camera and pick input side (0x2C bytes, vtable 00740208; RET 4, returns this).
__declspec(naked) inline void* __fastcall ScrollTextConstructOriginal(void* /*memory*/, void*, CInterface3D*, const char* /*text*/,
                                                                      int /*left*/, int /*top*/, int /*right*/, int /*bottom*/,
                                                                      float /*lineHeight*/) EI_ORIGINAL(0x0065CC00)
__declspec(naked) inline void* __fastcall ViewInputConstructOriginal(void* /*memory*/, void*, CInterface3D*) EI_ORIGINAL(0x005EACB0)
// 00607720 (rewritten as CBriefingScreen::LoadPersonTexts; through its address so the wrapper runs; plain RET).
// 00578A20 (the client glue 007AFEA5, not read; RET 0xC): whether exit `exit` can be taken now by the party whose unit
// records are listed in `records` (a CArray of record pointers); fills `second` (the same kind of array). Returns 0 / 1.
__declspec(naked) inline void __fastcall BriefingLoadPersonTextsOriginal(void* /*screen*/) EI_ORIGINAL(0x00607720)
__declspec(naked) inline int __fastcall GameCanLeaveZoneOriginal(void* /*glue*/, void*, void* /*records*/, int /*exit*/,
                                                                 void* /*second*/) EI_ORIGINAL(0x00578A20)

// A-I15. The client world (007AF5B8, thiscall, RET 4): 00565D00 the unit drawn at a screen point (the point's x, y; EAX
// the object or null); 00566100 selects `unit` (null: clears the selection; no result).
__declspec(naked) inline CEngineObject* __fastcall ClientUnitAtOriginal(void* /*world*/, void*, const int* /*point*/)
    EI_ORIGINAL(0x00565D00)
__declspec(naked) inline void __fastcall ClientWorldSelectOriginal(void* /*world*/, void*, void* /*unit*/)
    EI_ORIGINAL(0x00566100)
// The local view's orders (rewritten as CPlayerClient::SendWalk / SendAttack; through their address so the wrappers
// run): 0066A7D0 walk (units, point, run; RET 0xC), 0066A880 attack (units, target, how, ctrl; RET 0x10). `units` is an
// mfc::CArray<CUnit*>. Both return 1.
__declspec(naked) inline int __fastcall LocalViewSendWalkOriginal(void* /*view*/, void*, const void* /*units*/,
                                                                 const float* /*to*/, int /*run*/) EI_ORIGINAL(0x0066A7D0)
__declspec(naked) inline int __fastcall LocalViewSendAttackOriginal(void* /*view*/, void*, const void* /*units*/,
                                                                   void* /*target*/, int /*how*/, int /*ctrl*/)
    EI_ORIGINAL(0x0066A880)
// The interface's shutdown (005F4F90) calls: 00444BD0 (thiscall) the debug information panel
// (0079B930, string_map.cpp) frees its surface and font; 00462DF0 writes config\keyboard.ini
// (key_bindings.cpp).
__declspec(naked) inline void __fastcall DebugPanelReleaseOriginal(void* /*panel*/) EI_ORIGINAL(0x00444BD0)
// 00444C10 (thiscall, RET 4): the debug information panel's rectangle (string_map.cpp).
__declspec(naked) inline void __fastcall DebugPanelSetRectOriginal(void* /*panel*/, void*, const tagRECT* /*rect*/)
    EI_ORIGINAL(0x00444C10)
__declspec(naked) inline void __cdecl SaveKeyBindingsOriginal() EI_ORIGINAL(0x00462DF0)
// 005AC1A0 (thiscall on the model manager, UiModelManager(), RET 4): a new object `name` (a figure; null: an interface
// sprite, CUiMesh), or null.
__declspec(naked) inline CEngineObject* __fastcall ModelManagerNewObjectOriginal(void* /*manager*/, void*,
                                                                                 const char* /*name*/)
    EI_ORIGINAL(0x005AC1A0)
// 005A37C0 (thiscall, plain RET): a figure hands its quads (+0x1C0, count +0x1C4) to the renderer's buffer +0x188.
__declspec(naked) inline void __fastcall FigureUploadQuadsOriginal(void* /*figure*/) EI_ORIGINAL(0x005A37C0)
// 005E2A30 (thiscall on the chat overlay, CInterface3D +0x1C; RET 8): a chat line (`text` from player `number`) added.
__declspec(naked) inline void __fastcall ChatOverlayAddLineOriginal(void* /*chat*/, void*, const char* /*text*/,
                                                                   std::uint32_t /*number*/) EI_ORIGINAL(0x005E2A30)
// The console window (console::ConsoleWindow, thiscall; rewritten in console_window.cpp / ui_console_window.cpp, called
// through their address so the wrappers run): 005E3E90 SetRect(rect) (RET 4), 005E4800 DrawLines (plain RET).
__declspec(naked) inline void __fastcall ConsoleSetRectOriginal(void* /*console*/, void*, const int* /*rect*/)
    EI_ORIGINAL(0x005E3E90)
__declspec(naked) inline void __fastcall ConsoleDrawLinesOriginal(void* /*console*/) EI_ORIGINAL(0x005E4800)
// 005E3700 (thiscall on the chat overlay, plain RET; Rebirth-patched): the typed line drawn on its surface (hidden while
// the chat is closed), the caret clamped to it.
__declspec(naked) inline void __fastcall ChatOverlayDrawTypedOriginal(void* /*chat*/) EI_ORIGINAL(0x005E3700)
// 00578B20 (on the client glue 007AFEA5, ECX not read): the client leaves the interface screen for the world (message 5).
__declspec(naked) inline void __fastcall GlueBackToWorldOriginal(void* /*glue*/) EI_ORIGINAL(0x00578B20)
// 00661FE0 (thiscall on a CParty, plain RET): every item released (its slot 4), the item array's storage freed.
__declspec(naked) inline void __fastcall PartyReleaseItemsOriginal(void* /*party*/) EI_ORIGINAL(0x00661FE0)
// The game client's trade offer (ClientSession, thiscall on GameClient(); rewritten in net_client_*.cpp, called through
// their address so the wrappers run): 0041E0A0 records the trade with `party` (RET 4), 0041E180 takes the acceptance
// back, 0041E3E0 drops the offer, 0041DA50 publishes our offer (plain RET each).
__declspec(naked) inline void __fastcall ClientRecordTradeOriginal(void* /*client*/, void*, const void* /*party*/)
    EI_ORIGINAL(0x0041E0A0)
__declspec(naked) inline void __fastcall ClientCommitPartyOriginal(void* /*client*/) EI_ORIGINAL(0x0041E180)
__declspec(naked) inline void __fastcall ClientDropPartyOriginal(void* /*client*/) EI_ORIGINAL(0x0041E3E0)
__declspec(naked) inline void __fastcall ClientPublishPartyOriginal(void* /*client*/) EI_ORIGINAL(0x0041DA50)

