#pragma once

// The mouse cursor (docs/notes/ui.md, "Cursor"): the game's pictures, shown as the system's cursor.

#include <cstddef>

#include "lib/containers.h"
#include "lib/rect.h"
#include "platform/platform.h"

#include <array>
#include <cstdint>
#include <vector>

class TextureDevice;

// The cursor's shapes, in the order ui/ui_cursor.cpp makes their images.
enum class CursorShape : int {
    Default,
    Attack,
    Spell,
    Wait,
    Use,
    ScrollUp,
    ScrollUpRight,
    ScrollRight,
    ScrollDownRight,
    ScrollDown,
    ScrollDownLeft,
    ScrollLeft,
    ScrollUpLeft,
    Cancel,
    Talk,
    Move,
    AttackHead,        // the six attack cursors of the order keys: head, body, left and right hand, left and right leg
    AttackBody,
    AttackLeftHand,
    AttackRightHand,
    AttackLeftLeg,
    AttackRightLeg,
    Camera,
    SpellCancel,
    Steal,
};

// The cursor's size in pixels: 32, or 16 for the small cursors (static initialisers set 32).
inline std::int32_t g_cursorWidth = /*007C7C90*/ 32;
inline std::int32_t g_cursorHeight = /*007C7C94*/ 32;
struct Surface;

// The cursor's image (one per cursor shape): its surface holder (the picture: `frames` square frames side by
// side), a frame's size, the frame shown, the hot spot and the mask.
struct CursorImage {
    // The image of the picture "<name>.mmp" (from the texture manager), the
    // hot spot given in the 32-pixel layout.
    CursorImage(const char* name, int hotSpotX, int hotSpotY);
    ~CursorImage();
    CursorImage(const CursorImage&) = delete;
    CursorImage& operator=(const CursorImage&) = delete;

    Surface* holder = nullptr;
    std::int32_t width = 0;                  // a frame's width
    std::int32_t height = 0;
    int frames = 0;                  // frames side by side (the picture's width / height)
    int frame = 0;                   // the frame shown (stepped every 125 ms)
    Point hot;                       // the hot spot, scaled to the cursor's size
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

    // Unless no shape is selected yet or the cursor is not enabled (-1): shape `shape` and its image become
    // current with the image's current frame as `from`; returns the previous shape (a CursorShape, -1 for none).
    int SetShape(CursorShape shape);
    void ShowImage(bool shown);
    // Held from now on (or let go). Taking hold polls the mouse and drops its motion so far (from before the press,
    // which must not turn the camera) and keeps the pointer's position.
    void SetHeld(bool held);
    // The mouse's speed as the motion of a 60 Hz frame (kDragFrame), whatever the frame rate: the motion of the
    // polls in the last kDragFrame ms (or the last poll's interval when longer), scaled from that window to
    // kDragFrame; at 60 Hz the last poll's motion. Given once a poll, then (0, 0) until the next (the camera's turn,
    // docs/notes/ui.md "Camera mouse turn").
    Point TakeDrag();
    // Shape 0 (its image and frame).
    void Start();
    // Releases and frees every shape and empties the list, releases the device and resets the fields (not
    // enabled, shape -1).
    void Shutdown();
    // Steps the image's frame every 125 ms, adds the mouse's motion to motion (and keeps the poll for
    // TakeDrag), takes the mouse position
    // (or heldPosition while held) clamped into the bounds, places rect at the previous position less the
    // hot spot (the frame's size, clipped to the bounds, `from` clipped the same way) and keeps the new
    // position. Returns whether the frame stepped or the position changed.
    bool UpdatePosition();

    // Sets the cursor up on `device` (a reference taken, the old one's dropped); `smallCursors` picks 16-pixel cursors
    // instead of 32. Takes the bounds (the screen's size).
    void Init(TextureDevice* device, bool smallCursors);
    void AddShape(const char* name, int hotX, int hotY);

    TextureDevice* device = nullptr;    // referenced
    bool imageShown = false;         // an image is shown
    CursorImage* image = nullptr;
    int shape = 0;                   // the shape SetShape selected
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
    std::uint32_t pollTime = 0;              // when UpdatePosition last polled the mouse

    // The frame (ms) TakeDrag gives the motion for: the usual 60 Hz of the screens the turn was tuned on.
    static constexpr float kDragFrame = 1000.0f / 60.0f;

private:
    // The drag from the polls (TakeDrag).
    Point DragOfPolls() const;

    // A poll of the mouse: its interval since the one before (ms) and its motion.
    struct Poll {
        std::uint32_t interval = 0;
        Point motion = {};
    };
    // The last polls since the cursor was taken hold of, the newest at newestPoll (a ring; enough for a drag frame's
    // polls past 1000 a second).
    static constexpr int kPollsKept = 64;
    std::array<Poll, kPollsKept> polls = {};
    int newestPoll = 0;
    int pollCount = 0;
    Point drag = {};                         // TakeDrag's, from the last poll
    bool dragGiven = true;                   // TakeDrag gave it
};
// The frame's cursor handed to the renderer before the present, which shows it as the system's.
void CursorDrawFrame(Cursor* self);

extern Cursor g_cursor;

void SetUpCursors(int smallCursors);
void ResetCursors(int smallCursors);
