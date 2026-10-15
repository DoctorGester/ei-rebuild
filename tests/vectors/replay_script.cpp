// Replay of the "script" family (script_vectors.h) against src/game/script_parse.cpp. The keyword table
// and the lexer's strings come from the "@ScriptKeywords" fixture, the character classes from
// "@GameCrtCtype" (replay_crt.cpp); Rebirth's AddNewScriptCommands is off (the vectors are vanilla).
#include "crt_vectors.h"
#include "script_vectors.h"
#include "vectors.h"

#include "../../src/game/game_data.h"
#include "../../src/game/script.h"
#include "../../src/game/script_addon.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {

// CScript::Error's last report (the replay's Error, below).
struct ReportedError {
    bool reported = false;
    int line = 0, code = 0;
    char text[0x400] = "";
} g_error;

using namespace ei::vectors;
using namespace ei::vectors::script;

std::deque<std::string> g_strings;   // the fixture's strings (the table points into them)

const char* Keep(const std::string& text, bool isNull) {
    if (isNull) return nullptr;
    g_strings.push_back(text);
    return g_strings.back().c_str();
}

// Writes `size` bytes at `address` of the stand-in.
void WriteGameData(std::uintptr_t address, const void* data, std::size_t size) {
    std::memcpy(ei::ReplayGameData(address, size), data, size);
}

bool CheckKeywords(Reader& r, char*, std::size_t) {
    const std::uintptr_t tableAddress = r.Get<std::uint32_t>();
    const std::uint32_t count = r.Get<std::uint32_t>();
    auto* table = static_cast<ScriptKeyword*>(ei::ReplayGameData(tableAddress, (count + 1) * sizeof(ScriptKeyword)));
    for (std::uint32_t i = 0; i < count && r.ok(); ++i) {
        table[i].id = r.Get<std::int32_t>();
        bool isNull;
        std::string name = r.String(&isNull);
        table[i].name = Keep(name, isNull);
        std::string function = r.String(&isNull);
        table[i].function = Keep(function, isNull);
        std::string command = r.String(&isNull);
        table[i].command = Keep(command, isNull);
    }
    std::memset(&table[count], 0, sizeof(ScriptKeyword));
    const ScriptKeyword* where = table;
    WriteGameData(kKeywordTablePointer, &where, sizeof(where));
    const std::uint8_t quote = r.Get<std::uint8_t>();
    WriteGameData(kQuoteChar, &quote, 1);
    for (std::uint32_t i = 0, n = r.Get<std::uint32_t>(); i < n && r.ok(); ++i) {
        const std::uintptr_t at = r.Get<std::uint32_t>();
        bool isNull;
        std::string text = r.String(&isNull);
        const char* kept = Keep(text, isNull);
        WriteGameData(at, &kept, sizeof(kept));
    }
    for (std::uint32_t i = 0, n = r.Get<std::uint32_t>(); i < n && r.ok(); ++i) {
        const std::uintptr_t at = r.Get<std::uint32_t>();
        std::string text = r.String();
        WriteGameData(at, text.c_str(), text.size() + 1);
    }
    return true;
}
EI_VECTOR_CHECK_BYTES("@ScriptKeywords", CheckKeywords);

bool SameText(const char* a, const std::string& b, bool bNull) { return bNull ? a == nullptr : a && b == a; }

bool CheckClassify(Reader& r, char* d, std::size_t n) {
    const std::string text = r.String();
    const crt::NegativeClassesScope negative(r);
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

// A CScript the lexer can run on: it reads no member, it only reports errors (CScript::Error).
CScript* NoScript() {
    alignas(8) static unsigned char bytes[sizeof(CScript)];
    return reinterpret_cast<CScript*>(bytes);
}

bool CheckTokenize(Reader& r, char* d, std::size_t n) {
    const std::uint32_t length = r.Get<std::uint32_t>();
    const unsigned char* bytes = r.Bytes(length);
    const crt::NegativeClassesScope negative(r);
    const std::int32_t blockSize = r.Get<std::int32_t>();
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

    mfc::CString source(reinterpret_cast<const char*>(bytes), static_cast<int>(length));
    ScriptTokenList tokens{nullptr, nullptr, nullptr, 0, nullptr, nullptr, blockSize};
    g_error = ReportedError();
    const BOOL result = NoScript()->Tokenize(source, &tokens);

    std::uint64_t all = 0xCBF29CE484222325ull;
    std::uint32_t first = count0;   // the first token that differs
    std::uint32_t index = 0;
    const char* firstText = "";
    for (const ScriptTokenList::Node* node = tokens.head; node; node = node->next, ++index) {
        all = TokenHash(node->data.text, node->data.line, all);
        if (first == count0 && (index >= count0 || static_cast<std::uint32_t>(TokenHash(node->data.text, node->data.line)) != each0[index])) {
            first = index;
            firstText = node->data.text;
        }
    }
    bool ok = result == expected && static_cast<std::uint32_t>(tokens.count) == count0 && all == all0 && first == count0;
    if (ok && !expected)
        ok = g_error.reported && g_error.code == error0.code && g_error.line == error0.line &&
             std::strcmp(g_error.text, error0.text) == 0;
    if (!ok)
        std::snprintf(d, n, "%u bytes: ours %d with %d tokens, original %d with %u; first difference at token %u (ours \"%.40s\"); "
                      "error ours %d line %d \"%.60s\", original %d line %d \"%.60s\"",
                      length, result, tokens.count, expected, count0, first, firstText, g_error.code, g_error.line,
                      g_error.text, error0.code, error0.line, error0.text);
    for (ScriptTokenList::Node* node = tokens.head; node; node = node->next) node->data.text.~CString();
    mfc::PlexFreeDataChain(tokens.blocks);
    return ok;
}
EI_VECTOR_CHECK_BYTES("CScript::Tokenize", CheckTokenize);

bool CheckApplyDefines(Reader& r, char* d, std::size_t n) {
    const std::string before = r.String();
    const std::uint32_t count = r.Get<std::uint32_t>();
    std::vector<ScriptDefineList::Node> nodes(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::string name = r.String();
        const std::string value = r.String();
        new (&nodes[i].data.name) mfc::CString(name.c_str());
        new (&nodes[i].data.value) mfc::CString(value.c_str());
        nodes[i].prev = i ? &nodes[i - 1] : nullptr;
        nodes[i].next = i + 1 < count ? &nodes[i + 1] : nullptr;
    }
    const std::string expected = r.String();
    ScriptDefineList defines{nullptr, count ? &nodes[0] : nullptr, count ? &nodes[count - 1] : nullptr,
                             static_cast<int>(count), nullptr, nullptr, 10};
    mfc::CString line(before.c_str());
    ApplyDefines(&line, &defines);
    const bool ok = expected == static_cast<const char*>(line);
    if (!ok)
        std::snprintf(d, n, "\"%.100s\" with %u defines: ours \"%.100s\", original \"%.100s\"", before.c_str(), count,
                      static_cast<const char*>(line), expected.c_str());
    for (ScriptDefineList::Node& node : nodes) ScriptDefineElements::Destruct(&node.data, 1);
    return ok;
}
EI_VECTOR_CHECK_BYTES("ApplyDefines", CheckApplyDefines);

} // namespace

// The replay's CScript::Error (the game's is the original 004CF470, which Rebirth patches): the report
// is kept for the check (wvsprintfA's formats the lexer uses are printf's).
int __cdecl CScript::Error(CScript*, int line, int code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(g_error.text, sizeof(g_error.text), format, args);
    va_end(args);
    g_error.reported = true;
    g_error.line = line;
    g_error.code = code;
    return 0;
}

// Rebirth's AddNewScriptCommands stays off in the replay (script_addon.cpp needs the script runtime):
// the lexer asks Enabled() only.
namespace scriptaddon {
bool Enabled() { return false; }
const ScriptKeyword* Table() { std::abort(); }
bool RejectsMatch(const char*, char) { std::abort(); }
} // namespace scriptaddon
