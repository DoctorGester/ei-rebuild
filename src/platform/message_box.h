#pragma once

// The system's modal message box (docs/notes/platform.md).

#include <cstdint>

namespace platform {

enum class MessageButtons : std::uint8_t { Ok, OkCancel };
enum class MessageIcon : std::uint8_t { Error, Warning };
enum class MessageAnswer : std::uint8_t { Ok, Cancel };

// Shows `text` (UTF-8) under `caption` and waits for the player. `owner` is the native window it belongs to (null:
// none). Cancel only when the player cancelled; a box that could not be shown answers Ok.
MessageAnswer ShowMessageBox(void* owner, const char* text, const char* caption, MessageButtons buttons,
                             MessageIcon icon);

} // namespace platform
