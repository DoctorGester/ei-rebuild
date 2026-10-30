// The message box on Windows (platform/message_box.h). Through game.exe's import slot, which the image routes to the
// UTF-8 call.
#include "platform/message_box.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {

MessageAnswer ShowMessageBox(void* owner, const char* text, const char* caption, MessageButtons buttons,
                             MessageIcon icon) {
    const UINT style = (buttons == MessageButtons::OkCancel ? MB_OKCANCEL : MB_OK) |
                       (icon == MessageIcon::Error ? MB_ICONHAND : MB_ICONEXCLAMATION);
    const int answer = gameapi::MessageBoxA(static_cast<HWND>(owner), text, caption, style);
    return answer == IDCANCEL ? MessageAnswer::Cancel : MessageAnswer::Ok;
}

} // namespace platform
