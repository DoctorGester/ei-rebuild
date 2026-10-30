#pragma once

// The game's global objects as the harness reads and sets them (the application's globals, the cursor, the
// interface, the game flow, the options, the worlds, the scene renderer and its lock): our named globals, behind
// functions of the image side (image/image_side.h, image/harness_objects_image.cpp). Nothing here names a class of
// ours (ticket H30b).

#include "image/image_side.h"

#include <windows.h>

#include <cstdint>

// The application's globals (app.h).
inline const char* HarnessWindowTitle() { return ei::verify::image::WindowTitle(); }
inline HWND HarnessMainWindow() { return static_cast<HWND>(ei::verify::image::MainWindow()); }
inline std::uint8_t& HarnessInterfaceHasScreen() { return ei::verify::image::InterfaceHasScreen(); }
inline std::uint8_t& HarnessRecordingFrames() { return ei::verify::image::RecordingFrames(); }
inline bool HarnessActive() { return ei::verify::image::AppActive(); }

// The game flow's request (GameFlow::state: 0 none, 2 load, 3 new game) and the load request's slot.
inline int& HarnessFlowState() { return ei::verify::image::FlowState(); }
inline void HarnessSetLoadTarget(const char* slot) { ei::verify::image::SetLoadTarget(slot); }

// The options the harness sets: the tutorial windows off, the master server's host, the water's waves.
inline void HarnessClearShowTutorial() { ei::verify::image::ClearShowTutorial(); }
inline void HarnessSetMasterHost(const char* host) { ei::verify::image::SetMasterHost(host); }
inline void HarnessSetWaterWaves(bool on) { ei::verify::image::SetWaterWaves(on); }

// The cursor's state the cursor check logs and compares.
inline ei::verify::image::CursorState HarnessCursor() { return ei::verify::image::Cursor(); }

// The interface's window queued to come on top (null: none), its pending close-all and its screen fade (false
// without one).
inline void* HarnessQueuedWindow() { return ei::verify::image::QueuedWindow(); }
inline bool HarnessCloseAllPending() { return ei::verify::image::CloseAllPending(); }
inline bool HarnessScreenFade(bool* running, float* alpha) { return ei::verify::image::ScreenFade(running, alpha); }

// The server world's campaign mode: its session kind, which GameFlow::StartZone sets.
inline int HarnessCampaignMode() { return ei::verify::image::CampaignMode(); }

// The client world (for the game's picks; null-checked by them).
inline void* HarnessClientWorld() { return ei::verify::image::ClientWorldObject(); }

// The scene renderer's device (null before it is made) and the device's lock.
inline void* HarnessSceneDevice() { return ei::verify::image::SceneDevice(); }
inline _RTL_CRITICAL_SECTION* HarnessDeviceLock() {
    return static_cast<_RTL_CRITICAL_SECTION*>(ei::verify::image::DeviceLock());
}
