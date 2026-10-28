#pragma once

// The game's global objects as the harness reads and sets them (the application's globals, the cursor, the
// interface, the game flow, the options, the worlds, the scene renderer and its lock, the map database). In the image
// they are our named globals, behind functions of the image side (image/image_side.h, image/harness_objects_image.cpp);
// in --no-hooks runs the harness runs inside game.exe's own code, which never touches ours, so it reads game.exe's
// copy at the original's address through its views (exe_objects.h; docs/notes/standalone.md, "Named data (C18)").
// Nothing here names a class of ours (ticket H30b). The game's functions more than one harness file calls: ours in the
// image, game.exe's own in its runs (harness_original.h).

#include "exe_objects.h"
#include "harness_original.h"
#include "image/image_side.h"

#include "ei/hook.h"

#include <windows.h>

#include <cstdint>

namespace harness_objects {

// game.exe's objects (their addresses in game.exe 1.06).
inline ei::verify::exe::CInterface3D* Interface() {
    return &ei::OriginalGameData<ei::verify::exe::CInterface3D>(0x007C7D50);
}
inline ei::verify::exe::GameFlow* Flow() { return &ei::OriginalGameData<ei::verify::exe::GameFlow>(0x007AFE20); }
inline ei::verify::exe::options::GameOptions* Options() {
    return &ei::OriginalGameData<ei::verify::exe::options::GameOptions>(0x0079BC70);
}
inline ei::verify::exe::CCursor* Cursor() { return &ei::OriginalGameData<ei::verify::exe::CCursor>(0x007C7BD8); }
inline ei::verify::exe::CSceneRenderer* SceneRenderer() {
    return &ei::OriginalGameData<ei::verify::exe::CSceneRenderer>(0x007AF060);
}

// Sets one of game.exe's strings through its own CString::operator= (00700AF9: game.exe's allocator frees the old
// text and makes the new).
inline void SetString(ei::verify::exe::CString* string, const char* text) {
    ei::verify::OriginalCode<void*(__thiscall*)(ei::verify::exe::CString*, const char*)>(0x00700AF9)(string, text);
}

} // namespace harness_objects

// The application's globals (app.h).
inline const char* HarnessWindowTitle() {
    return ei::Standalone() ? ei::verify::image::WindowTitle() : &ei::OriginalGameData<const char>(0x0079BB24);
}
inline HWND HarnessMainWindow() {
    return ei::Standalone() ? static_cast<HWND>(ei::verify::image::MainWindow()) : ei::OriginalGameData<HWND>(0x0079BC28);
}
inline std::uint8_t& HarnessInterfaceHasScreen() {
    return ei::Standalone() ? ei::verify::image::InterfaceHasScreen() : ei::OriginalGameData<std::uint8_t>(0x00784750);
}
inline std::uint8_t& HarnessRecordingFrames() {
    return ei::Standalone() ? ei::verify::image::RecordingFrames() : ei::OriginalGameData<std::uint8_t>(0x0079BC54);
}
inline bool HarnessActive() {
    return ei::Standalone() ? ei::verify::image::AppActive() : ei::OriginalGameData<std::uint8_t>(0x0079BC5C) != 0;
}

// The game flow's request (GameFlow::state_: 0 none, 2 load, 3 new game) and the load request's slot.
inline int& HarnessFlowState() {
    return ei::Standalone() ? ei::verify::image::FlowState() : harness_objects::Flow()->state_;
}
inline void HarnessSetLoadTarget(const char* slot) {
    if (ei::Standalone()) ei::verify::image::SetLoadTarget(slot);
    else harness_objects::SetString(&harness_objects::Flow()->requestTarget_, slot);
}

// The options the harness sets: the tutorial windows off, the master server's host, the water's waves.
inline void HarnessClearShowTutorial() {
    if (ei::Standalone()) ei::verify::image::ClearShowTutorial();
    else harness_objects::Options()->showTutorial_ = 0;
}
inline void HarnessSetMasterHost(const char* host) {
    if (ei::Standalone()) ei::verify::image::SetMasterHost(host);
    else harness_objects::SetString(&harness_objects::Options()->masterServerName_, host);
}
inline void HarnessSetWaterWaves(bool on) {
    if (ei::Standalone()) ei::verify::image::SetWaterWaves(on);
    else harness_objects::Options()->enableWaterWaves_ = on;
}

// The cursor's state the cursor check logs and compares.
inline ei::verify::image::CursorState HarnessCursor() {
    if (ei::Standalone()) return ei::verify::image::Cursor();
    const ei::verify::exe::CCursor* cursor = harness_objects::Cursor();
    ei::verify::image::CursorState state;
    state.mode = cursor->mode_;
    state.thread = cursor->thread_ != nullptr;
    state.imageShown = cursor->imageShown_;
    if (const ei::verify::exe::CursorImage* image = cursor->image_) {
        state.image = true;
        state.hotX = image->hotX;
        state.hotY = image->hotY;
    }
    return state;
}

// The interface's window queued to come on top (null: none), its pending close-all, its screen fade (false without
// one) and its layout size.
inline void* HarnessQueuedWindow() {
    return ei::Standalone() ? ei::verify::image::QueuedWindow() : harness_objects::Interface()->queuedWindow_;
}
inline bool HarnessCloseAllPending() {
    return ei::Standalone() ? ei::verify::image::CloseAllPending() : harness_objects::Interface()->closeAll_;
}
inline bool HarnessScreenFade(bool* running, float* alpha) {
    if (ei::Standalone()) return ei::verify::image::ScreenFade(running, alpha);
    const ei::verify::exe::CScreenFade* fade = harness_objects::Interface()->fade_;
    if (!fade) return false;
    *running = fade->active_ != 0;
    *alpha = fade->alpha_;
    return true;
}

// The server world's campaign mode: its session kind (007C2CC4), which GameFlow::StartZone sets.
inline int HarnessCampaignMode() {
    return ei::Standalone() ? ei::verify::image::CampaignMode() : ei::verify::exe::ServerWorld()->sessionKind_;
}

// The client world (for the game's picks; null-checked by them).
inline void* HarnessClientWorld() {
    return ei::Standalone() ? ei::verify::image::ClientWorldObject() : static_cast<void*>(ei::verify::exe::ClientWorld());
}

// The scene renderer's device (null before it is made) and the device's lock.
inline void* HarnessSceneDevice() {
    return ei::Standalone() ? ei::verify::image::SceneDevice() : harness_objects::SceneRenderer()->device_.device;
}
inline _RTL_CRITICAL_SECTION* HarnessDeviceLock() {
    return ei::Standalone() ? static_cast<_RTL_CRITICAL_SECTION*>(ei::verify::image::DeviceLock())
                            : &ei::OriginalGameData<_RTL_CRITICAL_SECTION>(0x007AEE78);
}

