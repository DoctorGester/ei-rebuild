// Running the generated static initialisers in the standalone image (static_init.h; docs/notes/standalone.md).
#include "static_init.h"

namespace ei::static_init {

void RunStaticInitialisers() {
    Run run;
    StaticInitialisers(run);
}

} // namespace ei::static_init
