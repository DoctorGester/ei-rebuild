// Replay of the "script" family (script_vectors.h) against src/game/script_parse.cpp. The keyword table
// and the lexer's strings are ours, compared with the "@ScriptKeywords" fixture; the character classes are
// text_case.h's; Rebirth's AddNewScriptCommands is off (the vectors are vanilla).
#include "script_vectors.h"
#include "vectors.h"

#include "../../src/game/game_data.h"
#include "../../src/game/script.h"
#include "../../src/game/script_addon.h"
#include "../../src/game/script_parse.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

// Script::Error's last report (the replay's Error, below).
struct ReportedError {
    bool reported = false;
    int line = 0, code = 0;
    char text[0x400] = "";
} g_error;

using namespace ei::vectors;
using namespace ei::vectors::script;

// Writes `size` bytes at `address` of the stand-in.
void WriteGameData(std::uintptr_t address, const void* data, std::size_t size) {
    std::memcpy(ei::ReplayGameData(address, size), data, size);
}

bool SameText(const char* a, const std::string& b, bool bNull) { return bNull ? a == nullptr : a && b == a; }

// The table, the quote and the comment markers are ours (kScriptKeywords, scriptparse's lexer
// constants): compared with the recording. The texts at fixed addresses go into the stand-in.
bool CheckKeywords(Reader& r, char* detail, std::size_t size) {
    constexpr std::uint32_t kOurs = static_cast<std::uint32_t>(std::size(kScriptKeywords) - 1);
    r.Get<std::uint32_t>();   // the table's address in game.exe
    const std::uint32_t count = r.Get<std::uint32_t>();
    bool same = count == kOurs;
    if (!same) std::snprintf(detail, size, "%u keywords recorded, %u ours", count, kOurs);
    for (std::uint32_t i = 0; i < count && r.ok(); ++i) {
        const int id = r.Get<std::int32_t>();
        bool nameNull, functionNull, commandNull;
        const std::string name = r.String(&nameNull);
        const std::string function = r.String(&functionNull);
        const std::string command = r.String(&commandNull);
        if (!same) continue;
        const ScriptKeyword& ours = kScriptKeywords[i];
        if (static_cast<int>(ours.id) != id || !SameText(ours.name, name, nameNull) || !SameText(ours.function, function, functionNull) ||
            !SameText(ours.command, command, commandNull)) {
            std::snprintf(detail, size, "keyword %u: recorded %s %d, ours %s %d", i, name.c_str(), id,
                          ours.name ? ours.name : "(null)", static_cast<int>(ours.id));
            same = false;
        }
    }
    const std::uint8_t quote = r.Get<std::uint8_t>();
    if (quote != static_cast<std::uint8_t>(scriptparse::kQuote)) {
        std::snprintf(detail, size, "quote %02X recorded", quote);
        same = false;
    }
    const char* const comments[] = {scriptparse::kCommentOpen, scriptparse::kCommentClose, scriptparse::kLineComment};
    for (std::uint32_t i = 0, n = r.Get<std::uint32_t>(); i < n && r.ok(); ++i) {
        r.Get<std::uint32_t>();   // the pointer's address in game.exe
        bool isNull;
        const std::string text = r.String(&isNull);
        if (i >= std::size(comments) || !SameText(comments[i], text, isNull)) {
            std::snprintf(detail, size, "comment marker %u: recorded \"%s\"", i, text.c_str());
            same = false;
        }
    }
    for (std::uint32_t i = 0, n = r.Get<std::uint32_t>(); i < n && r.ok(); ++i) {
        const std::uintptr_t at = r.Get<std::uint32_t>();
        std::string text = r.String();
        WriteGameData(at, text.c_str(), text.size() + 1);
    }
    return same;
}
EI_VECTOR_CHECK_BYTES("@ScriptKeywords", CheckKeywords);

bool CheckClassify(Reader& r, char* d, std::size_t n) {
    const std::string text = r.String();
    const std::uint8_t asked = r.Get<std::uint8_t>();
    const std::int32_t id0 = r.Get<std::int32_t>();
    const std::int32_t rest0 = r.Get<std::int32_t>();
    int id = kUnasked, rest = kUnasked;
    ClassifyToken(text.c_str(), (asked & 1) ? &id : nullptr, (asked & 2) ? &rest : nullptr);
    if (id == id0 && rest == rest0) return true;
    std::snprintf(d, n, "\"%s\": ours id %X rest %d, original id %X rest %d", text.c_str(), id, rest, id0, rest0);
    return false;
}
EI_VECTOR_CHECK_BYTES("ClassifyToken", CheckClassify);

bool CheckSignature(Reader& r, char* d, std::size_t n) {
    const std::int32_t id = r.Get<std::int32_t>();
    const std::int32_t type = r.Get<std::int32_t>();
    bool isNull;
    const std::string expected = r.String(&isNull);
    const char* actual = FunctionSignature(id, type);
    if (SameText(actual, expected, isNull)) return true;
    std::snprintf(d, n, "id %X type %d: ours %s, original %s", id, type, actual ? actual : "(null)",
                  isNull ? "(null)" : expected.c_str());
    return false;
}
EI_VECTOR_CHECK_BYTES("FunctionSignature", CheckSignature);

bool CheckKeywordName(Reader& r, char* d, std::size_t n) {
    const std::int32_t id = r.Get<std::int32_t>();
    bool isNull;
    const std::string expected = r.String(&isNull);
    const char* actual = KeywordName(id);
    if (SameText(actual, expected, isNull)) return true;
    std::snprintf(d, n, "id %X: ours %s, original %s", id, actual ? actual : "(null)", isNull ? "(null)" : expected.c_str());
    return false;
}
EI_VECTOR_CHECK_BYTES("KeywordName", CheckKeywordName);

// A Script the lexer can run on: it reads no member, it only reports errors (Script::Error).
Script* NoScript() {
    static Script script;
    return &script;
}

bool CheckTokenize(Reader& r, char* d, std::size_t n) {
    const std::uint32_t length = r.Get<std::uint32_t>();
    const unsigned char* bytes = r.Bytes(length);
    r.Get<std::int32_t>();   // the token list's block size
    const std::int32_t expected = r.Get<std::int32_t>();
    ReportedError error0;
    if (!expected) {
        error0.code = r.Get<std::int32_t>();
        error0.line = r.Get<std::int32_t>();
        const std::string text = r.String();
        std::snprintf(error0.text, sizeof(error0.text), "%s", text.c_str());
    }
    const std::uint32_t count0 = r.Get<std::uint32_t>();
    const std::uint64_t all0 = r.Get<std::uint64_t>();
    std::vector<std::uint32_t> each0(count0);
    for (std::uint32_t& h : each0) h = r.Get<std::uint32_t>();
    if (!bytes || !r.ok()) return true;   // RunBytes reports the short payload

    const std::string source(reinterpret_cast<const char*>(bytes), length);
    std::list<ScriptToken> tokens;
    g_error = ReportedError();
    const int result = NoScript()->Tokenize(source, &tokens);

    std::uint64_t all = 0xCBF29CE484222325ull;
    std::uint32_t first = count0;   // the first token that differs
    std::uint32_t index = 0;
    const char* firstText = "";
    for (const ScriptToken& token : tokens) {
        all = TokenHash(token.text.c_str(), token.line, all);
        if (first == count0 && (index >= count0 || static_cast<std::uint32_t>(TokenHash(token.text.c_str(), token.line)) != each0[index])) {
            first = index;
            firstText = token.text.c_str();
        }
        ++index;
    }
    bool ok = result == expected && tokens.size() == count0 && all == all0 && first == count0;
    if (ok && !expected)
        ok = g_error.reported && g_error.code == error0.code && g_error.line == error0.line &&
             std::strcmp(g_error.text, error0.text) == 0;
    if (!ok)
        std::snprintf(d, n, "%u bytes: ours %d with %d tokens, original %d with %u; first difference at token %u (ours \"%.40s\"); "
                      "error ours %d line %d \"%.60s\", original %d line %d \"%.60s\"",
                      length, result, static_cast<int>(tokens.size()), expected, count0, first, firstText, g_error.code, g_error.line,
                      g_error.text, error0.code, error0.line, error0.text);
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::Tokenize", CheckTokenize);

bool CheckApplyDefines(Reader& r, char* d, std::size_t n) {
    const std::string before = r.String();
    const std::uint32_t count = r.Get<std::uint32_t>();
    std::list<ScriptDefine> defines;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string name = r.String();
        std::string value = r.String();
        defines.push_back({std::move(name), std::move(value)});
    }
    const std::string expected = r.String();
    std::string line = before;
    ApplyDefines(&line, &defines);
    const bool ok = expected == line;
    if (!ok)
        std::snprintf(d, n, "\"%.100s\" with %u defines: ours \"%.100s\", original \"%.100s\"", before.c_str(), count,
                      line.c_str(), expected.c_str());
    return ok;
}
EI_VECTOR_CHECK_BYTES("ApplyDefines", CheckApplyDefines);

} // namespace

// The replay's Script::Error (the game's is the original 004CF470, which Rebirth patches): the report
// is kept for the check and set on the script as ScriptErrorV sets it (wvsprintfA's formats the
// parser uses, %s and %i, are printf's).
int Script::Error(Script* script, int line, int code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(g_error.text, sizeof(g_error.text), format, args);
    va_end(args);
    g_error.reported = true;
    g_error.line = line;
    g_error.code = code;
    script->errorLine = line;
    script->errorText = g_error.text;
    script->errorCode = code;
    return 0;
}

// Rebirth's AddNewScriptCommands stays off in the replay (script_addon.cpp needs the script runtime):
// the lexer asks Enabled() only.
namespace scriptaddon {
bool Enabled() { return false; }
const ScriptKeyword* Table() { std::abort(); }
bool RejectsMatch(const char*, char) { std::abort(); }
void SignatureStep(SSCCmd*, const char**, int) { std::abort(); }
}

// Script::LoadFile's file reader (script_text_file.cpp needs the game's files): not replayed.
int scriptparse::ReadTextFile(const std::string&, std::string*) { std::abort(); } // namespace scriptaddon
