// The CScript constructor and destructor and CScript::GetError. See script.h and docs/notes/script.md.
// They run for the world's two scripts only, from the world's constructor and destructor.
#include "script.h"

#include "world_glue.h"

CScript::CScript() : pointLights_(0x100), tempGroups_(1), tempObjects_(1) { ScriptReset(this); }

// The reset leaves no effect (the point light slots are zeroed), so the members' destructors drop none.
CScript::~CScript() { ScriptReset(this); }

ScriptErrorInfo* CScript::GetError(ScriptErrorInfo* out) {
    out->text = errorText_;
    out->code = errorCode_;
    out->line = errorLine_;
    return out;
}
