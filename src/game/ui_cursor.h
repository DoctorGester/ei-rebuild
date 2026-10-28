#pragma once

// The software mouse cursor (docs/notes/ui.md, "Cursor").

#include <cstddef>

#include "lib/containers.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstdint>

#include <vector>
#include <windows.h>

class CTextureDevice;

// The cursor's size in pixels: 32, or 16 for the small cursors (static initialisers set 32).
inline LONG g_cursorWidth = /*007C7C90*/ 32;
inline LONG g_cursorHeight = /*007C7C94*/ 32;
struct CDXSurface;
namespace mfc::impl { struct WinThread; }

struct IDirectInput7A;
struct IDirectInputDevice7A;

unsigned __cdecl CursorThreadEntry(void* cursor);

// The DirectInput mouse (made by the application's start, which also sets it up).
class CDirectInputMouse final {
public:
    CDirectInputMouse() {}
    virtual ~CDirectInputMouse();


    CDirectInputMouse(const CDirectInputMouse&) = delete;
    CDirectInputMouse& operator=(const CDirectInputMouse&) = delete;

    // DirectInput 7 for `instance`, its system mouse device with the mouse data format, non-exclusive in the
    // background for `window`, acquired (a failed Acquire is reported and passed over). Each other failure is
    // reported (FatalErrorCode) and gives its result as 0 / 1.
    int Setup(HWND window, HINSTANCE instance);
    void Shutdown();
    // Acquires or unacquires the device with the window's activation; 0 without a device, the report's result
    // as 0 / 1 when Acquire fails.
    int SetActive(char active);
    // Reads the device state (DIMOUSESTATE: lX, lY, lZ, buttons; zeroed when the read fails) into the object
    // and returns it. The argument is not read.
    const LONG* Poll(int unused);
    const LONG* PollThroughHook(int unused) { return Poll(unused); }
    // The mouse data format Setup gives the device (dinput.lib's c_dfDIMouse, as a DIDATAFORMAT), our own
    // copy; the objects name the game's GUID_XAxis / YAxis / ZAxis.
    static const void* MouseDataFormat();

    std::uint32_t references_ = 0;           // the count of holders (WinMain keeps one)
    IDirectInput7A* input_ = nullptr;        // counted
    IDirectInputDevice7A* device_ = nullptr; // counted
    LONG state_[4];
};

// The cursor's image (one per cursor shape): its surface holder (the picture: `frames` square frames side by
// side), a frame's size, the frame shown, the hot spot and the mask.
struct CursorImage {
    // The image of the picture "<name>.mmp" (from the texture manager; loaded into a holder with `flag`), the
    // hot spot given in the 32-pixel layout. Returns this.
    CursorImage* Construct(const char* name, int hotSpotX, int hotSpotY, int flag);
    void Destroy();

    CDXSurface* holder;
    LONG width;                      // a frame's width
    LONG height;
    int frames;                      // frames side by side (the picture's width / height)
    int frame;                       // the frame shown (stepped every 125 ms)
    int hotX;                        // the hot spot, scaled to the cursor's size
    int hotY;
    std::uint8_t* saved;             // the mask over the whole picture: all ones where the picture is black, else 0
};
static_assert(offsetof(CursorImage, width) == 0x04);
static_assert(offsetof(CursorImage, frames) == 0x0C);
static_assert(offsetof(CursorImage, hotX) == 0x14);
static_assert(offsetof(CursorImage, saved) == 0x1C);
static_assert(sizeof(CursorImage) == 0x20);

class CCursor {
public:
    void SaveBackground(char fromPrimary);
    int SetShape(int shape) { return ChangeShape(shape); }

    CCursor* Construct();
    void DestroyBody();
    // Unless no shape is selected yet or the cursor is not enabled (-1): under the lock, shape `shape` and
    // its image become current with the image's current frame as `from_`; returns the previous shape.
    int ChangeShape(int shape);
    void ShowImage(int shown);
    void SetHeld(char held);
    void TakeLock();
    void ReleaseLock();
    // Shape 0 (its image and frame) under the lock; unless in mode 1 the drawing thread started
    // (AfxBeginThread at priority 2).
    void Start();
    // Ends the drawing thread (stop_, then waits for `stopped_`), closes the event, releases and frees every
    // shape and empties the list, releases the surfaces and the device and resets the fields (not enabled,
    // shape -1, mode 0).
    void Shutdown();
    // Steps the image's frame every 125 ms, adds the DirectInput motion to motion_, takes the mouse position
    // (or heldPosition_ while held) clamped into the bounds, places rect_ at the previous position less the
    // hot spot (the frame's size, clipped to the bounds, `from_` clipped the same way) and keeps the new
    // position. Returns whether the frame stepped or the position changed.
    char UpdatePosition();

    // Sets the cursor up on `device` (a reference taken, the old one's dropped) in `mode` with `mouse`;
    // `smallCursors` (low byte) picks 16-pixel cursors instead of 32. Makes the end event, the bounds (the
    // render target's size when there is a primary surface) and the background and composition surfaces
    // (cursor size and twice that).
    void Init(CTextureDevice* device, int mode, CDirectInputMouse* mouse, int smallCursors);
    void AddShape(const char* name, int hotX, int hotY, int flag);

    int mode_;                       // 2: the background is saved from the primary without an image
    CTextureDevice* device_;         // referenced
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
    CDXSurface* under_;              // the background under the cursor
    CDXSurface* target_;             // where the cursor is drawn
    std::vector<CursorImage*> images_;   // the shapes (AddShape)
    POINT position_;                 // the position drawn last
    POINT heldPosition_;             // the position kept while held
    POINT motion_;                   // the mouse motion summed
    std::uint8_t held_;              // the cursor stays at heldPosition_
    std::uint8_t enabled_;           // the cursor exists (created)
    std::uint8_t unused_92[2];
    DWORD frameTime_;                // when the image's frame last stepped
    mfc::impl::WinThread* thread_;   // the drawing thread, or null
    int stop_;                       // asks the cursor thread to end
    CRITICAL_SECTION lock_;
};
// The software cursor drawn into the render target before the present.
void CursorDrawFrame(CCursor* self);
static_assert(offsetof(CCursor, imageShown_) == 0x0C);
static_assert(offsetof(CCursor, shape_) == 0x18);
static_assert(offsetof(CCursor, stopped_) == 0x1C);
static_assert(offsetof(CCursor, drawn_) == 0x20);
static_assert(offsetof(CCursor, bounds_) == 0x50);
static_assert(offsetof(CCursor, under_) == 0x60);


extern lib::StaticObject<CCursor> g_cursor;

void SetUpCursors(bool modeTwo, CDirectInputMouse* context, int smallCursors, int flags);
void ResetCursors(bool modeTwo, CDirectInputMouse* context, int smallCursors, int flags);
