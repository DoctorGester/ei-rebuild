#pragma once

// The software mouse cursor (docs/notes/ui.md, "Cursor").

#include <cstddef>

#include "lib/containers.h"
#include "lib/rect.h"
#include "platform/platform.h"

#include <cstdint>
#include <mutex>
#include <vector>

class TextureDevice;

// The cursor's size in pixels: 32, or 16 for the small cursors (static initialisers set 32).
inline std::int32_t g_cursorWidth = /*007C7C90*/ 32;
inline std::int32_t g_cursorHeight = /*007C7C94*/ 32;
struct Surface;

unsigned CursorThreadEntry(void* cursor);

// The DirectInput mouse (made by the application's start, which also sets it up).
class DirectInputMouse final {
public:
    DirectInputMouse() {}
    virtual ~DirectInputMouse();

    DirectInputMouse(const DirectInputMouse&) = delete;
    DirectInputMouse& operator=(const DirectInputMouse&) = delete;

    // The mouse device for `window` and `instance` (platform::MouseDevice::Open). A failure is reported
    // (FatalErrorCode, except a failed Acquire, which is passed over) and gives its result as 0 / 1.
    int Setup(platform::NativeWindow window, platform::NativeInstance instance);
    void Shutdown();
    // Acquires or unacquires the device with the window's activation; 0 without a device, the report's result
    // as 0 / 1 when Acquire fails.
    int SetActive(bool active);
    // Reads the device state (x, y and wheel motion, buttons; zeroed when the read fails) into the object and
    // returns it.
    const std::int32_t* Poll();

    std::uint32_t references = 0;            // the count of holders (WinMain keeps one)
    platform::MouseDevice device;
    std::int32_t state[4];
};

// The cursor's image (one per cursor shape): its surface holder (the picture: `frames` square frames side by
// side), a frame's size, the frame shown, the hot spot and the mask.
struct CursorImage {
    // The image of the picture "<name>.mmp" (from the texture manager), the
    // hot spot given in the 32-pixel layout.
    CursorImage(const char* name, int hotSpotX, int hotSpotY);
    ~CursorImage();
    CursorImage(const CursorImage&) = delete;
    CursorImage& operator=(const CursorImage&) = delete;

    Surface* holder;
    std::int32_t width;                      // a frame's width
    std::int32_t height;
    int frames;                      // frames side by side (the picture's width / height)
    int frame;                       // the frame shown (stepped every 125 ms)
    int hotX;                        // the hot spot, scaled to the cursor's size
    int hotY;
    std::vector<std::uint8_t> saved; // the mask over the whole picture: all ones where the picture is black, else 0
};

class Cursor {
public:
    // Not enabled, no device.
    Cursor() = default;
    // The device released.
    ~Cursor();
    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    // Unless no shape is selected yet or the cursor is not enabled (-1): under the lock, shape `shape` and
    // its image become current with the image's current frame as `from`; returns the previous shape.
    int SetShape(int shape);
    void ShowImage(bool shown);
    void SetHeld(bool held);
    void TakeLock();
    void ReleaseLock();
    // Shape 0 (its image and frame) under the lock; unless in mode 1 the drawing thread started
    // (platform::Thread at priority 2).
    void Start();
    // Ends the drawing thread (stop, then waits for `stopped`), closes the event, releases and frees every
    // shape and empties the list, releases the surfaces and the device and resets the fields (not enabled,
    // shape -1, mode 0).
    void Shutdown();
    // Steps the image's frame every 125 ms, adds the DirectInput motion to motion, takes the mouse position
    // (or heldPosition while held) clamped into the bounds, places rect at the previous position less the
    // hot spot (the frame's size, clipped to the bounds, `from` clipped the same way) and keeps the new
    // position. Returns whether the frame stepped or the position changed.
    bool UpdatePosition();

    // Sets the cursor up on `device` (a reference taken, the old one's dropped) in `mode` with `mouse`;
    // `smallCursors` picks 16-pixel cursors instead of 32. Makes the end event and the bounds (the screen's size).
    void Init(TextureDevice* device, int mode, DirectInputMouse* mouse, bool smallCursors);
    void AddShape(const char* name, int hotX, int hotY);

    int mode = 0;                    // 2: the background is saved from the primary without an image
    TextureDevice* device = nullptr;    // referenced
    DirectInputMouse* mouse = nullptr;    // polled for the motion
    bool imageShown = false;         // an image is shown
    CursorImage* image = nullptr;
    std::uint32_t imageSpare = 0;    // 0 from Init and Shutdown (with image); nothing reads it
    int shape = 0;                   // the shape SetShape selected
    platform::Signal stopped;         // set when the cursor thread ends
    Rect drawn = {};                 // the rectangle drawn last
    Rect rect = {};                  // the cursor's screen rectangle
    Rect from = {};                  // the part of the image shown
    Rect bounds = {};
    std::vector<CursorImage*> images;    // the shapes (AddShape)
    Point position = {};             // the position drawn last
    Point heldPosition = {};         // the position kept while held
    Point motion = {};               // the mouse motion summed
    bool held = false;               // the cursor stays at heldPosition
    bool enabled = false;            // the cursor exists (created)
    std::uint32_t frameTime = 0;             // when the image's frame last stepped
    platform::Thread thread;         // the drawing thread, when started
    int stop = 0;                    // asks the cursor thread to end
    std::recursive_mutex lock;
};
// The software cursor drawn into the render target before the present.
void CursorDrawFrame(Cursor* self);

extern Cursor g_cursor;

void SetUpCursors(bool modeTwo, DirectInputMouse* context, int smallCursors);
void ResetCursors(bool modeTwo, DirectInputMouse* context, int smallCursors);
