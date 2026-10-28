// Running the generated static initialisers in the standalone image (static_init.h; docs/notes/standalone.md).
#include "static_init.h"

#include "lib/crt.h"

namespace ei::static_init {

void Run::AtExit(void(__cdecl* fn)()) { libcrt::AtExit(fn); }

void RunStaticInitialisers() {
    Run run;
    StaticInitialisers(run);
}

} // namespace ei::static_init
