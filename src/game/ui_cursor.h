#pragma once

// The software mouse cursor (the object, see docs/notes/ui.md,
// "Cursor"). Only the members the draws use; the object is created by original code.

#include <cstddef>

#include "lib/containers.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstdint>

#include <windows.h>

class CTextureDevice;

// The cursor's size in pixels: 32, or 16 for the small cursors (static initialisers set 32).
inline LONG g_cursorWidth = /*007C7C90*/ 32;
inline LONG g_cursorHeight = /*007C7C94*/ 32;
struct CDXSurface;
namespace mfc::impl { struct WinThread; }

struct IDirectInput7A;
struct IDirectInputDevice7A;

// The drawing thread's procedure (the cursor),
// started by AfxBeginThread.
unsigned __cdecl CursorThreadEntry(void* cursor);

// The DirectInput mouse (*; made by the application's start,
// which also sets it up). 0x20 bytes. input_mouse.cpp
class CDirectInputMouse final {
public:
    // WinMain's: no references, no DirectInput or device.
    CDirectInputMouse() {}
    virtual ~CDirectInputMouse();

    // On the game's heap (app_lifetime.cpp).
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    CDirectInputMouse(const CDirectInputMouse&) = delete;
    CDirectInputMouse& operator=(const CDirectInputMouse&) = delete;

    // DirectInput 7 for `instance`, its system mouse device with the mouse data
    // format, non-exclusive in the background for `window`, acquired (a failed Acquire is reported
    // and passed over). Each other failure is reported (FatalErrorCode) and gives its result as
    // 0 / 1; success sets only AL.
    int Setup(HWND window, HINSTANCE instance);
    // The device unacquired and released, DirectInput released.
    void Shutdown();
    // Acquires or unacquires the device with
    // the window's activation; 0 without a device, the report's result as 0 / 1 when Acquire fails.
    int SetActive(char active);
    // Reads the device state (DIMOUSESTATE: lX, lY, lZ, buttons; zeroed when the
    // read fails) into the object and returns it. The argument is not read. PollThroughHook goes
    // through the address so the wrapper runs.
    const LONG* Poll(int unused);
    const LONG* PollThroughHook(int unused) { return Poll(unused); }
    // The mouse data format Setup gives the device (dinput.lib's c_dfDIMouse, as a
    // DIDATAFORMAT): our own, since the object table lives in game.exe's code section,
    // which the standalone image does not have. The objects name the game's GUID_XAxis / YAxis / ZAxis.
    static const void* MouseDataFormat();

    std::uint32_t references_ = 0;           // the count of holders (WinMain keeps one)
    IDirectInput7A* input_ = nullptr;        // counted
    IDirectInputDevice7A* device_ = nullptr; // counted
    LONG state_[4];                          // DIMOUSESTATE
};

// The cursor's image (what +0x10 points at; made for every cursor shape):
// its surface holder (the picture: `frames` square frames side by side), a frame's size, the frame
// shown, the hot spot and the mask.
struct CursorImage {
    // The image of the picture "<name>" (the file's name with ".mmp", from the
    // texture manager; loaded into a holder with `flag`), the hot spot given in the
    // 32-pixel layout. Returns this.
    CursorImage* Construct(const char* name, int hotSpotX, int hotSpotY, int flag);
    // Destructor body: the mask freed, the holder released.
    void Destroy();

    CDXSurface* holder;              // surface holder
    LONG width;                      // a frame's width
    LONG height;
    int frames;                      // frames side by side (the picture's width / height)
    int frame;                       // the frame shown (steps it every 125 ms)
    int hotX;                        // the hot spot, scaled to the cursor's size
    int hotY;
    std::uint8_t* saved;             // the mask over the whole picture (its pixel size): all
                                     //       ones where the picture is black, else 0
};
static_assert(offsetof(CursorImage, width) == 0x04);
static_assert(offsetof(CursorImage, frames) == 0x0C);
static_assert(offsetof(CursorImage, hotX) == 0x14);
static_assert(offsetof(CursorImage, saved) == 0x1C);
static_assert(sizeof(CursorImage) == 0x20);

class CCursor {
public:
    void SaveBackground(char fromPrimary);
    // Selects cursor shape `shape`; returns the
    // previous one (-1 when the cursor is not set up).
    int SetShape(int shape) { return ChangeShape(shape); }

    // Constructor body: no device, surfaces or shapes, the lock initialised, not
    // enabled. Returns this.
    CCursor* Construct();
    // Destructor body: the lock deleted, the shape list's storage freed, the two
    // surfaces and the device released (the fields left as they are).
    void DestroyBody();
    // Unless no shape is selected yet or the cursor is not enabled (-1): under
    // the lock, shape `shape` and its image become current with the image's current frame as
    // `from_`; returns the previous shape.
    int ChangeShape(int shape);
    // Under the lock, whether an image is shown (when enabled).
    void ShowImage(int shown);
    // Under the lock, the held byte (when enabled).
    void SetHeld(char held);
    // The lock taken / released, only when enabled in mode 2.
    void TakeLock();
    void ReleaseLock();
    // Shape 0 (its image and frame) under the lock; unless in mode 1 the drawing thread
    // started (AfxBeginThread at priority 2).
    void Start();
    // Ends the drawing thread (stop_, then waits for `stopped_`), closes the event,
    // releases and frees every shape and empties the list, releases the surfaces and the device
    // and resets the fields (not enabled, shape -1, mode 0).
    void Shutdown();
    // Steps the image's frame every 125 ms, adds the DirectInput motion
    // to motion_, takes the mouse position (or heldPosition_ while held) clamped into the bounds,
    // places rect_ at the previous position less the hot spot (the frame's size, clipped to the
    // bounds, `from_` clipped the same way) and keeps the new position. Returns whether the frame
    // stepped or the position changed.
    char UpdatePosition();

    // Sets the cursor up on `device` (a reference
    // taken, the old one's dropped) in `mode` with `mouse`; `smallCursors` (low byte) picks 16-pixel
    // cursors instead of 32 (g_cursorWidth / g_cursorHeight). Makes the end event, the
    // bounds (the render target's size when there is a primary surface) and the background and
    // composition surfaces (cursor size and twice that).
    void Init(CTextureDevice* device, int mode, CDirectInputMouse* mouse, int smallCursors);
    // Adds the shape made from the picture "<name>.mmp"
    // with the hot spot (hotX, hotY) and `flag` (CursorImage::Construct; null when the allocation
    // fails) to the shape list.
    void AddShape(const char* name, int hotX, int hotY, int flag);

    int mode_;                       // 2: the background is saved from the primary without an image
    CTextureDevice* device_;         // the device object (its +0x0C IDirectDraw7), referenced
    CDirectInputMouse* mouse_;       // polled for the motion
    int imageShown_;                 // an image is shown
    CursorImage* image_;
    std::uint32_t imageSpare_;       // 0 from Init and Shutdown (with image_); nothing reads it
    int shape_;                      // the shape SetShape selected
    HANDLE stopped_;                 // set when the cursor thread ends
    RECT drawn_;                     // the rectangle drawn last
    RECT rect_;                      // the cursor's screen rectangle
    RECT from_;                      // the part of the image shown
    RECT bounds_;
    CDXSurface* under_;              // surface holder: the background under the cursor
    CDXSurface* target_;             // surface holder: where the cursor is drawn
    lib::AlignedArray<CursorImage*> images_;   // the shapes (AddShape)
    POINT position_;                 // the position drawn last
    POINT heldPosition_;             // the position kept while held
    POINT motion_;                   // the mouse motion summed
    std::uint8_t held_;              // the cursor stays at +0x80 (sets it)
    std::uint8_t enabled_;           // the cursor exists (created)
    std::uint8_t unused_92[2];
    DWORD frameTime_;                // when the image's frame last stepped
    mfc::impl::WinThread* thread_;   // the drawing thread, or null
    int stop_;                       // asks the cursor thread to end
    CRITICAL_SECTION lock_;
};
// The software cursor drawn into the render target before the present.
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

// The game's cursor (the object; ui_cursor_setup.cpp): built by the static initialiser in the
// list's order, destroyed by the exit function it registers.
extern lib::StaticObject<CCursor> g_cursor;

// The cursor initialised and given its shapes.
void __fastcall SetUpCursors(bool modeTwo, int context, int smallCursors, int flags);
// The cursor shut down and set up again.
void __fastcall ResetCursors(bool modeTwo, int context, int smallCursors, int flags);
