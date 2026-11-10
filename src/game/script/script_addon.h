#pragma once

// Rebirth's extra script commands and functions (addon.dll's AddNewScriptCommands, v0.12.4.2; docs/notes/rebirth.md
// "Scripting and console"), always on: 69 script functions and commands added to the keyword table, seven of them
// replacing a game one, the '?' (optional parameter) and '+' (repeat the previous type) signature letters, and a
// stricter keyword match. Internal to script_addon*.cpp and the script functions that call into it.

#include "game/script/script.h"

namespace scriptaddon {

// What a handler is passed. Calc functions: `result` is CalcFloat's local float, CalcObject's ScriptObjectRef**
// (holding the new temporary), CalcGroup's ScriptGroup** (holding the new temporary group) or
// CalcString's std::string*. Commands (ScriptRun::RunCommand): `result` is null.
struct Call {
    // The result slot, of the type the calling Calc function gives it (none for a command).
    struct Result {
        Result(std::nullptr_t) {}
        Result(float* slot) : number(slot) {}
        Result(std::string* slot) : text(slot) {}
        Result(ScriptObjectRef** slot) : object(slot) {}
        Result(ScriptGroup** slot) : group(slot) {}
        union {
            float* number = nullptr;
            std::string* text;
            ScriptObjectRef** object;
            ScriptGroup** group;
        };
    };

    ScriptObjectRef* ObjectResult() const { return *result.object; }
    ScriptGroup* GroupResult() const { return *result.group; }

    ScriptCall* cmd = nullptr;
    Script* script = nullptr;
    Result result;
};

// The extended keyword table, built once: the game's entries (ConsoleString, Add, Mul and Random with the longer
// signature, their handlers leaving the game's argument counts to the game's code; IsAlive, IsDead and ResetTarget
// the game's own), then the new ones (ids 300 + record index), then the terminator.
const ScriptKeyword* Table();

// The handler of `call->cmd->id` when it is one of Rebirth's, run; false when there is none or the handler
// declines (then the game's own code for the id runs).
bool Dispatch(Call* call);

// ClassifyToken: a keyword starting with a letter does not match when the text goes on with a letter, a digit,
// '_' or '#' (the match still sets the best length).
bool RejectsMatch(const char* keyword, char next);

// LoadFunction, after the separator token is classified: the signature's '?' and '+'. At ')' a '+' is passed and a
// '?' passes the optional parameters' letters; otherwise a '+' steps back to the repeated type and a '?' is skipped.
void SignatureStep(std::string_view signature, std::size_t* letter, int tokenId);

// The string functions' CalcString (the Lua bindings' 'S' functions): Rebirth's string function (none: the game
// ends).
void CalcString(ScriptCall* cmd, Script* script, std::string* result);

} // namespace scriptaddon
