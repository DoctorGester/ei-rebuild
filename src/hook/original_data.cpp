// The import table calls go through (lib/import_table.h) in ei_harness.dll, which runs only in game.exe: game.exe's
// own import address table, as its headers place it. The image's own: src/standalone/image_imports.cpp.
#include "lib/import_table.h"

#include <windows.h>

#include <cstdint>

namespace ei {

ImportTable& Imports() {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& iat = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    return *reinterpret_cast<ImportTable*>(base + iat.VirtualAddress);
}

} // namespace ei
