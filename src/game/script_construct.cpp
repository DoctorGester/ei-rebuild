// The CScript constructor and destructor and CScript::GetError. See script.h and docs/notes/script.md.
// They run for the world's two scripts only, from the world's constructor and destructor.
#include "script.h"

#include "world_glue.h"

CScript::CScript() : pointLights(0x100), tempGroups(1), tempObjects(1) { ScriptReset(this); }

// The reset leaves no effect (the point light slots are zeroed), so the members' destructors drop none.
CScript::~CScript() { ScriptReset(this); }

ScriptErrorInfo* CScript::GetError(ScriptErrorInfo* out) {
    out->text = errorText;
    out->code = errorCode;
    out->line = errorLine;
    return out;
}
