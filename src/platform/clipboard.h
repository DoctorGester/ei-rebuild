#pragma once

// The system's clipboard, for text (docs/notes/platform.md). Text is UTF-8.

#include <string>
#include <string_view>

namespace platform {

// Puts `text` on the clipboard in place of what it held; nothing changes when the clipboard is busy.
void CopyText(std::string_view text);
// The clipboard's text; empty when it holds none or is busy.
std::string ClipboardText();

} // namespace platform
