#pragma once

// Rebirth's AddNewScriptCommands (addon.dll v0.12.4.2), reproduced behind its flag
// (include/ei/rebirth.h, docs/notes/rebirth.md "Scripting and console"): 69 script functions and
// commands added to the keyword table, seven of them replacing a game one, the '?' (optional
// parameter) and '+' (repeat the previous type) signature letters, and a stricter keyword match.
// Internal to script_addon*.cpp and the script functions that call into it.

#include "script.h"

namespace scriptaddon {

// What addon passes its handlers (built by the hooks at 004B425A and the others, called through
// 10021D96). Calc functions: `result` is CalcFloat's float*, CalcObject's ScriptObjectRef**
// (holding the new temporary), CalcGroup's ScriptGroup** (holding the new temporary group) or
// CalcString's mfc::CString*. Commands (ScriptRun::Execute): `cont` is the command's "go on"
// flag (1 on entry) and `run` the running script; `result` is null.
struct Call {
    SSCCmd* cmd;
    CScript* script;
    void* result;
    int* cont;
    ScriptRun* run;
};

// Patch::AddNewScriptCommands.
bool Enabled();

// The extended keyword table, built once like addon builds it: the game's entries, the eight
// replaced ones with addon's signature, then the new ones (ids 300 + record index), then the
// terminator.
const ScriptKeyword* Table();

// The handler of `call->cmd->id` when it is one of addon's, run; false when there is none or the
// handler declines (then the game's own code for the id runs, as after addon's hooks).
bool Dispatch(Call* call);

// ClassifyToken (hook at 004CF6F0): a keyword starting with a letter does not match when the
// text goes on with a letter, a digit, '_' or '#' (the match still sets the best length).
bool RejectsMatch(const char* keyword, char next);

// LoadFunction (hook at 004D02CC, after the separator token is classified): the signature's '?'
// and '+'. At ')' a '+' is passed and a '?' drops the optional parameters' slots; otherwise a '+'
// steps back to the repeated type (adding a slot for it) and a '?' is skipped.
void SignatureStep(SSCCmd* cmd, const char** signature, int tokenId);

// SSCCmd::CalcString (004B5650, which the patched CalcParameters calls for a string argument):
// prepares the arguments, then addon's string function or the game's fatal error.
void CalcString(SSCCmd* cmd, CScript* script, ScriptVars* vars, mfc::CString* result);

} // namespace scriptaddon
