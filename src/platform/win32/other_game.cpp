// The other game on Windows (platform.h, BringOtherInstanceForward): a game that runs already, its window found by
// class and title, is brought to the front. An isolated run (seams.h) counts none.
#include "platform/platform.h"

#include "platform/seams.h"
#include "platform/win32/platform_win32.h"

namespace platform {

namespace {

// Another program's window `other` brought to the front, restored from the task bar, unless it is in front already;
// false for none (null).
bool BringForward(HWND other) {
    if (!other) return false;
    const DWORD foregroundThread = ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr);
    const DWORD otherThread = ::GetWindowThreadProcessId(other, nullptr);
    if (otherThread == foregroundThread && !::IsIconic(other)) return true;
    HWND popup = ::GetLastActivePopup(other);
    if (::IsIconic(popup)) ::ShowWindow(popup, SW_RESTORE);
    ::BringWindowToTop(popup);
    ::SetForegroundWindow(popup);
    return true;
}

} // namespace

bool BringOtherInstanceForward(const char* name, const char* title) {
    return !IsolatedRun() && BringForward(FindWindowW(text::Wide(name).c_str(), text::Wide(title).c_str()));
}

} // namespace platform
