// The folder of the map files (0055E990, A-U12). Rebirth patches it (a mod folder's maps): this is
// the vanilla function and installs in the vanilla launch only (rule 9); its callers (the zone
// loaders, themselves patched or not rewritten) reach it through its address.
#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>
#include <new>

namespace {
constexpr std::uintptr_t kDataFolder = std::uintptr_t(0x007AF460);   // CString: the game's data path
} // namespace

// 0055E990 (RET 4: the result's storage; ECX unused). A copy of the data path with "maps\" appended,
// constructed at `out`. Returns `out`.
mfc::CString* __stdcall MapsFolder(mfc::CString* out) {
    mfc::CString folder(*reinterpret_cast<const mfc::CString*>(kDataFolder));
    return new (out) mfc::CString(folder + "maps\\");
}
EI_REPLACE(0x0055E990, MapsFolder);
