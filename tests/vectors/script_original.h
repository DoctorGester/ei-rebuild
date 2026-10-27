#pragma once

// What the script recorders read of game.exe's own CScript where its layout and ours differ: game.exe keeps the error
// as a CString followed by the code and the line, at the offset where ours has errorText_ (a std::string, larger);
// the members before it are laid out alike.

#include "../../src/game/script.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

struct OriginalScriptError {
    mfc::CString text;
    std::int32_t code;
    std::int32_t line;
};

inline const OriginalScriptError& OriginalError(const CScript* script) {
    return *reinterpret_cast<const OriginalScriptError*>(reinterpret_cast<const char*>(script) +
                                                         offsetof(CScript, errorText_));
}
