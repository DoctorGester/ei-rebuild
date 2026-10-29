// ei_harness.dll's entry (docs/notes/standalone.md section 5): ei_launch injects the DLL into game.exe, the original
// game, before its own code runs. The standalone image links the harness in (verify build) and has no DLL.
#include "ei/hook.h"
#include "log.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        ei::Log("ei_harness loaded into pid %lu", GetCurrentProcessId());
        ei::RedirectGamePaths();
        ei::InstallHooksAtWinMain();
    }
    return TRUE;
}
