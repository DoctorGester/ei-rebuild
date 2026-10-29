// The Script constructor and destructor and Script::GetError. See script.h and docs/notes/script.md.
// They run for the world's two scripts only, from the world's constructor and destructor.
#include "script.h"

#include "world_glue.h"

Script::Script() : pointLights(0x100), tempGroups(1), tempObjects(1) { ScriptReset(this); }

// The reset leaves no effect (the point light slots are zeroed), so the members' destructors drop none.
Script::~Script() { ScriptReset(this); }

ScriptErrorInfo* Script::GetError(ScriptErrorInfo* out) {
    out->text = errorText;
    out->code = errorCode;
    out->line = errorLine;
    return out;
}
