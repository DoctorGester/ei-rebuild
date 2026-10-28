// The AddressSanitizer build (build.cmd asan; docs/notes/harness.md, "AddressSanitizer"): its reports go to
// asan.<pid> in the run's folder, where tools/run_all.py finds them, instead of the game's stderr, which no one reads.
// Set when ei_hook.dll loads, before the game starts. Nothing in the other builds.
#if defined(__SANITIZE_ADDRESS__)

#include "../../../src/hook/log.h"

#include <windows.h>

extern "C" void __sanitizer_set_report_path(const char* path);   // the ASan runtime's

namespace {

const bool g_reportPath = [] {
    char path[MAX_PATH];
    ei::OutputPath("asan", path);
    __sanitizer_set_report_path(path);
    return true;
}();

} // namespace

#endif
