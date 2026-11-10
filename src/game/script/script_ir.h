#pragma once

// The parse tree of one script text (Script::Load): what the loader builds into command blocks, definitions and
// globals, with the names, lines and the order the text writes them in. The parser fills it beside its own
// structures; nothing runs it. The Lua writer reads it (docs/notes/lua-scripting.md, "Design"). Dump prints it, and
// DumpLoaded prints what a load built into a Script in the same form, so the two can be compared.

#include "game/script/script.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace scriptir {

struct Call;
struct Block;

// What fills an argument slot.
enum class ArgKind {
    Number,   // a number literal
    Text,     // a quoted string
    Local,    // a parameter of the enclosing script
    Global,   // a global variable
    Call,     // a nested function call
};

// One argument as written: its slot (the n-th argument of its type) and what fills it.
struct Arg {
    ScriptType type = ScriptType::None;
    int slot = 0;
    ArgKind kind = ArgKind::Number;
    std::string token;              // the token as written
    int line = 0;
    float number = 0.0f;            // Number: the value atof gives
    std::string text;               // Text: without the quotes
    std::string name;               // Local / Global: the variable's name as declared (qualified for a global)
    int index = 0;                  // Local / Global: its index among the variables of its type
    std::unique_ptr<Call> call;     // Call
};

// A command or function call. Assign (`global = value`) and CallScript (a declared script) name their target.
struct Call {
    ScriptFunction id = ScriptFunction::None;
    std::string token;              // the keyword, script or variable name as written
    int line = 0;
    std::vector<Arg> args;          // in the written order
    int target = 0;                 // Assign: the global's index; CallScript: the definition's index
    ScriptType targetType = ScriptType::None;   // Assign
    std::string targetName;         // Assign: the global's name; CallScript: the definition's name
    std::unique_ptr<Block> body;    // For / ForIf
};

// A command block: a then-section, a For body, conditions or a WorldScript, with the block id the loader gave it.
struct Block {
    int id = 0;
    std::vector<Call> calls;
};

// An `if(...)then(...)` pair.
struct Rule {
    int line = 0;
    Block conditions;
    Block commands;
};

struct Variable {
    std::string name;
    ScriptType type = ScriptType::None;
    int line = 0;
};

// A script definition this load declared or gave its body.
struct Definition {
    int index = 0;                  // in Script::definitions
    std::string name;               // qualified
    std::vector<Variable> params;   // in the written order
    int line = 0;                   // of the declaration (Script or DeclareScript)
    int bodyLine = 0;               // of the `Script` section with the body, 0 when there is none
    std::vector<Rule> rules;
};

struct Define {
    std::string name;
    std::string value;
    int line = 0;
    int uses = 0;                   // replacements made in the text after it
};

// One Script::Load.
struct File {
    int fileCount = 0;              // Script::fileCount when the load started (the `!n!` of its names)
    std::vector<Define> defines;    // in the order they are applied
    std::vector<Variable> globals;  // GlobalVars, qualified
    std::vector<Definition> definitions;
    std::vector<Block> worldScripts;   // at most one unless the load failed on the second
    int lines = 0;                  // of the preprocessed text
    int tokens = 0;
    bool ok = false;
    ScriptErrorInfo error;          // when !ok
};

// What a Script holds before a load, so that DumpLoaded prints only what the load added.
struct LoadMark {
    std::size_t definitions = 0;
    std::vector<std::size_t> rules;   // each definition's if-then pair count
    std::size_t globals[4] = {};      // per type
    std::size_t worldBlocks = 0;
};
LoadMark Mark(const Script& script);

// The name of a variable (empty when the set has no name for it).
const std::string& VariableName(const ScriptVars& vars, ScriptType type, int index);
// A definition's parameters in the written order (the type letters of `params` with the names of `vars`).
std::vector<Variable> Params(const ScriptDefinition& definition);

// The text form of the tree: the globals, the WorldScripts and the definitions, each call's arguments by type and
// slot (the loader keeps no other order), numbers in their shortest exact form. `lines` adds each call's, rule's and
// definition's source line.
std::string Dump(const File& file, bool lines = false);
// What a load added to `script` since `mark`, in Dump's form: the same text when the tree holds everything the
// loader built.
std::string DumpLoaded(const Script& script, const LoadMark& mark);

} // namespace scriptir
