#pragma once

// The software mouse cursor (the object at 007C7BD8; 005E4E00-005E6990, see docs/notes/ui.md,
// "Cursor"). Only the members the rewritten draws use; the object is created by original code.

#include <cstddef>
#include "originals_ui.h"

#include "lib/containers.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstdint>

#include <windows.h>

class CTextureDevice;
struct CDXSurface;
namespace mfc::impl { struct WinThread; }

struct IDirectInput7A;
struct IDirectInputDevice7A;

// 005E52A0 (cdecl; ui_cursor_frame.cpp): the drawing thread's procedure (the cursor; runs 005E5690),
// started by AfxBeginThread.
unsigned __cdecl CursorThreadEntry(void* cursor);

// The DirectInput mouse (*0079BA08, vtable 0073C350; made by the application's start 00456440,
// which also sets it up). 0x20 bytes. input_mouse.cpp (A-H25).
class CDirectInputMouse {
public:
    // 0065EAD0 (RET 8). DirectInput 7 for `instance`, its system mouse device with the mouse data
    // format, non-exclusive in the background for `window`, acquired (a failed Acquire is reported
    // and passed over). Each other failure is reported (FatalErrorCode) and gives its result as
    // 0 / 1; success sets only AL.
    int Setup(HWND window, HINSTANCE instance);
    // 0065EC30. The device unacquired and released, DirectInput released.
    void Shutdown();
    // 0065EC70 (RET 4; sets only AL unless Acquire fails). Acquires or unacquires the device with
    // the window's activation; 0 without a device, the report's result as 0 / 1 when Acquire fails.
    int SetActive(char active);
    // 0065ECB0 (RET 4). Reads the device state (DIMOUSESTATE: lX, lY, lZ, buttons; zeroed when the
    // read fails) into the object and returns it. The argument is not read. PollThroughHook goes
    // through the address so the wrapper runs.
    const LONG* Poll(int unused);
    const LONG* PollThroughHook(int unused) { return static_cast<CDirectInputMouse*>(this)->Poll(unused); }
    // 004578D0 (A-E1; app_lifetime.cpp). Destructor body: the vtable, Shutdown, the two counted
    // pointers' releases (both already null), the root vtable (0073C368).
    void Destruct();
    // 004578B0 (slot 0, RET 4). Destruct, then operator delete when bit 0 of `flags` is set.
    CDirectInputMouse* DeletingDestruct(unsigned flags);

    // The mouse data format Setup gives the device (dinput.lib's c_dfDIMouse, 00745D00, as a
    // DIDATAFORMAT): ours, since game.exe keeps its object table (006E6180) in its code section,
    // which the standalone image does not have. The objects name the game's GUID_XAxis / YAxis / ZAxis.
    static const void* MouseDataFormat();

    void* vtable_;                   // +0x00
    std::uint32_t references_;       // +0x04 the count of holders (WinMain keeps one in 0079BA08)
    IDirectInput7A* input_;          // +0x08 counted
    IDirectInputDevice7A* device_;   // +0x0C counted
    LONG state_[4];                  // +0x10 DIMOUSESTATE
};
static_assert(offsetof(CDirectInputMouse, device_) == 0x0C);
static_assert(sizeof(CDirectInputMouse) == 0x20);

// The cursor's image (what +0x10 points at; made by 005E4EB0 for every cursor shape, from 005E63F0):
// its surface holder (the picture: `frames` square frames side by side), a frame's size, the frame
// shown, the hot spot and the mask.
struct CursorImage {
    // 005E4EB0 (RET 0x10): the image of the picture "<name>" (the file's name with ".mmp", from the
    // texture manager; loaded into a holder by 004EFC60 with `flag`), the hot spot given in the
    // 32-pixel layout. Returns this.
    CursorImage* Construct(const char* name, int hotSpotX, int hotSpotY, int flag);
    // 005E5160 (ui_cursor_setup.cpp). Destructor body: the mask freed, the holder released.
    void Destroy();

    CDXSurface* holder;              // +0x00 surface holder
    LONG width;                      // +0x04 a frame's width
    LONG height;                     // +0x08
    int frames;                      // +0x0C frames side by side (the picture's width / height)
    int frame;                       // +0x10 the frame shown (005E52E0 steps it every 125 ms)
    int hotX;                        // +0x14 the hot spot, scaled to the cursor's size
    int hotY;                        // +0x18
    std::uint8_t* saved;             // +0x1C the mask over the whole picture (its pixel size): all
                                     //       ones where the picture is black, else 0
};
static_assert(offsetof(CursorImage, width) == 0x04);
static_assert(offsetof(CursorImage, frames) == 0x0C);
static_assert(offsetof(CursorImage, hotX) == 0x14);
static_assert(offsetof(CursorImage, saved) == 0x1C);
static_assert(sizeof(CursorImage) == 0x20);

class CCursor {
public:
    // 0x005E6630 (ui_cursor_frame.cpp).
    void SaveBackground(char fromPrimary);
    // 005E5200 (ChangeShape). Selects cursor shape `shape`; returns the
    // previous one (-1 when the cursor is not set up).
    int SetShape(int shape) { return ChangeShape(shape); }

    // ui_cursor_setup.cpp:
    // 005E51C0. Constructor body: no device, surfaces or shapes, the lock initialised, not
    // enabled. Returns this.
    CCursor* Construct();
    // 005E4E00. Destructor body: the lock deleted, the shape list's storage freed, the two
    // surfaces and the device released (the fields left as they are).
    void DestroyBody();
    // 005E5200 (RET 4). Unless no shape is selected yet or the cursor is not enabled (-1): under
    // the lock, shape `shape` and its image become current with the image's current frame as
    // `from_`; returns the previous shape.
    int ChangeShape(int shape);
    // 005E5270 (RET 4). Under the lock, whether an image is shown (when enabled).
    void ShowImage(int shown);
    // 005E67B0 (RET 4). Under the lock, the held byte (when enabled).
    void SetHeld(char held);
    // 005E65F0 / 005E6610. The lock taken / released, only when enabled in mode 2.
    void TakeLock();
    void ReleaseLock();
    // 005E6260. Shape 0 (its image and frame) under the lock; unless in mode 1 the drawing thread
    // started (AfxBeginThread of 005E52A0 at priority 2).
    void Start();
    // 005E62E0. Ends the drawing thread (stop_, then waits for `stopped_`), closes the event,
    // releases and frees every shape and empties the list, releases the surfaces and the device
    // and resets the fields (not enabled, shape -1, mode 0).
    void Shutdown();
    // 005E52E0 (plain RET, AL). Steps the image's frame every 125 ms, adds the DirectInput motion
    // to motion_, takes the mouse position (or heldPosition_ while held) clamped into the bounds,
    // places rect_ at the previous position less the hot spot (the frame's size, clipped to the
    // bounds, `from_` clipped the same way) and keeps the new position. Returns whether the frame
    // stepped or the position changed.
    char UpdatePosition();

    // 005E60B0 (RET 0x10; ui_cursor_setup.cpp). Sets the cursor up on `device` (a reference
    // taken, the old one's dropped) in `mode` with `mouse`; `smallCursors` (low byte) picks 16-pixel
    // cursors instead of 32 (the size globals 007C7C90 / 007C7C94). Makes the end event, the
    // bounds (the render target's size when there is a primary surface) and the background and
    // composition surfaces (cursor size and twice that, through 004EF7B0).
    void Init(CTextureDevice* device, int mode, CDirectInputMouse* mouse, int smallCursors);
    // 005E63F0 (RET 0x10; ui_cursor_setup.cpp). Adds the shape made from the picture "<name>.mmp"
    // with the hot spot (hotX, hotY) and `flag` (CursorImage::Construct; null when the allocation
    // fails) to the shape list.
    void AddShape(const char* name, int hotX, int hotY, int flag);

    int mode_;                       // +0x00 2: the background is saved from the primary without an image
    CTextureDevice* device_;         // +0x04 the device object (its +0x0C IDirectDraw7), referenced
    CDirectInputMouse* mouse_;       // +0x08 polled for the motion (005E52E0)
    int imageShown_;                 // +0x0C an image is shown (005E5270)
    CursorImage* image_;             // +0x10
    std::uint32_t imageSpare_;       // +0x14 0 from Init and Shutdown (with image_); nothing reads it
    int shape_;                      // +0x18 the shape SetShape selected
    HANDLE stopped_;                 // +0x1C set when the cursor thread ends
    RECT drawn_;                     // +0x20 the rectangle drawn last
    RECT rect_;                      // +0x30 the cursor's screen rectangle (005E52E0)
    RECT from_;                      // +0x40 the part of the image shown
    RECT bounds_;                    // +0x50
    CDXSurface* under_;              // +0x60 surface holder: the background under the cursor
    CDXSurface* target_;             // +0x64 surface holder: where the cursor is drawn
    lib::AlignedArray<CursorImage*> images_;   // +0x68 the shapes (AddShape)
    POINT position_;                 // +0x78 the position drawn last (005E52E0)
    POINT heldPosition_;             // +0x80 the position kept while held
    POINT motion_;                   // +0x88 the mouse motion summed by 005E52E0
    std::uint8_t held_;              // +0x90 the cursor stays at +0x80 (005E67B0 sets it)
    std::uint8_t enabled_;           // +0x91 the cursor exists (created)
    std::uint8_t unused_92[2];
    DWORD frameTime_;                // +0x94 when the image's frame last stepped
    mfc::impl::WinThread* thread_;   // +0x98 the drawing thread, or null
    int stop_;                       // +0x9C asks the cursor thread to end
    CRITICAL_SECTION lock_;          // +0xA0
};
// 005E54A0 (ui_cursor_frame.cpp): the software cursor drawn into the render target before the present.
void __fastcall CursorDrawFrame(CCursor* self);
static_assert(offsetof(CCursor, imageShown_) == 0x0C);
static_assert(offsetof(CCursor, shape_) == 0x18);
static_assert(offsetof(CCursor, stopped_) == 0x1C);
static_assert(offsetof(CCursor, drawn_) == 0x20);
static_assert(offsetof(CCursor, bounds_) == 0x50);
static_assert(offsetof(CCursor, under_) == 0x60);
static_assert(offsetof(CCursor, enabled_) == 0x91 && offsetof(CCursor, images_) == 0x68 && offsetof(CCursor, motion_) == 0x88);
static_assert(offsetof(CCursor, frameTime_) == 0x94 && offsetof(CCursor, thread_) == 0x98);
static_assert(offsetof(CCursor, stop_) == 0x9C);
static_assert(offsetof(CCursor, lock_) == 0xA0);

static_assert(sizeof(CCursor) == 0xB8);

// The game's cursor (the object at 007C7BD8; ui_cursor_setup.cpp): built by the static initialiser 005E4DD0 in the
// list's order, destroyed by the exit function it registers (005E4DF0).
extern lib::StaticObject<CCursor> g_cursor;
