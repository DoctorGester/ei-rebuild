// Script loader, part 1: tokens, the lexer (CScript::Tokenize), keyword lookup, #define handling
// and name helpers. The parser proper is in script_parse_load.cpp. See script.h and
// docs/notes/script.md ("Loader and parser").
//
// Calls between loader functions go through the original addresses (forwarders below): the
// keyword lookups are patched by Rebirth, and in verify builds each callee then runs through its
// own wrapper.
#include "script.h"
#include "script_parse.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders

// ---- Game CRT and MFC helpers (local to the loader) ---------------------------------------------

namespace scriptparse {

__declspec(naked) int __cdecl IsDigit(int) EI_ORIGINAL(0x006E9379)
__declspec(naked) int __cdecl IsSpace(int) EI_ORIGINAL(0x006E93CE)
__declspec(naked) int __cdecl Strncmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006E9180)
__declspec(naked) int __cdecl Mbscmp(const char*, const char*) EI_ORIGINAL(0x006E848B)
__declspec(naked) double __cdecl Atof(const char*) EI_ORIGINAL(0x006E911A)

__declspec(naked) StringOps* StringOps::InitN(const char*, int) EI_ORIGINAL(0x006FD4DE)
__declspec(naked) mfc::CString StringOps::Mid(int, int) const EI_ORIGINAL(0x006FD9D7)
__declspec(naked) mfc::CString StringOps::Left(int) const EI_ORIGINAL(0x006FDAE9)
__declspec(naked) mfc::CString StringOps::Right(int) const EI_ORIGINAL(0x006FDA6D)
__declspec(naked) int StringOps::Find(const char*) const EI_ORIGINAL(0x006FDBD9)
__declspec(naked) char* StringOps::LockBuffer() EI_ORIGINAL(0x00700E6A)
__declspec(naked) void StringOps::UnlockBuffer() EI_ORIGINAL(0x00700E7C)

// Through the original addresses (Rebirth patches the keyword lookups).
__declspec(naked) void __fastcall ClassifyAt(const char*, int*, int*) EI_ORIGINAL(0x004CF600)
__declspec(naked) const char* __fastcall SignatureAt(int, int) EI_ORIGINAL(0x004D0B10)
__declspec(naked) const char* __fastcall KeywordNameAt(int) EI_ORIGINAL(0x004D0BA0)

} // namespace scriptparse

using namespace scriptparse;

namespace {

// Lexer constants (pointers and the quote character are data in game.exe).
char QuoteChar() { return *reinterpret_cast<const char*>(0x0078BC98); }                     // '"'
const char* CommentOpen() { return *reinterpret_cast<const char* const*>(0x0078BC8C); }     // "/*"
const char* CommentClose() { return *reinterpret_cast<const char* const*>(0x0078BC90); }    // "*/"
const char* LineComment() { return *reinterpret_cast<const char* const*>(0x0078BC94); }     // "//"

// Takes a node from the list's free chain, carving a block of `blockSize` nodes when it is empty
// (CList::NewNode as the game inlines it). Shared by the token and define lists (16-byte nodes).
template <typename List>
typename List::Node* TakeNode(List* list) {
    using Node = typename List::Node;
    static_assert(sizeof(Node) == 16);
    if (!list->free) {
        mfc::CPlex* block = mfc::PlexCreate(list->blocks, list->blockSize, sizeof(Node));
        Node* node = reinterpret_cast<Node*>(block + 1) + (list->blockSize - 1);
        for (int i = list->blockSize - 1; i >= 0; --i, --node) {
            node->next = list->free;
            list->free = node;
        }
    }
    Node* node = list->free;
    list->free = node->next;
    return node;
}

} // namespace

// ---- Keyword table ------------------------------------------------------------------------------

const ScriptKeyword* KeywordTable() {
    // ClassifyToken loads the table with MOV EBP,imm32 at 004CF69A; Rebirth rewrites that
    // immediate to point at its extended table (and the other references the same way).
    return *reinterpret_cast<const ScriptKeyword* const*>(0x004CF69B);
}

void __fastcall ClassifyToken(const char* text, int* id, int* rest) {
    const char* p = text;
    if (IsDigit(*p) || *p == '.' || *p == '-') {
        // Number: digits and at most one '.', after an optional leading '-' or '.'.
        int dots = *p == '.';
        ++p;
        while (*p) {
            if (!IsDigit(*p) && *p != '.') break;
            if (*p == '.' && dots++ != 0) break;
            ++p;
        }
        if (id) *id = kLexNumber;
        if (rest) {
            *rest = 0;
            for (; *p; ++p) ++*rest;
        }
        return;
    }
    const char quote = QuoteChar();
    if (*p == quote) {
        // String: up to the closing quote; *rest counts what follows it.
        ++p;
        while (*p && *p != quote) ++p;
        if (*p) {
            if (id) *id = kLexString;
            if (rest) {
                *rest = -1;
                for (; *p; ++p) ++*rest;
            }
            return;
        }
        // Unterminated: the keyword search below runs on the end of the text (finds nothing).
    }
    if (rest) *rest = 0;
    if (id) *id = 0;
    int best = -1;
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        int length = static_cast<int>(std::strlen(k->name));
        if (length < best) continue;
        if (gamecrt::strnicmp(p, k->name, length) != 0) continue;
        if (rest) *rest = static_cast<int>(std::strlen(p)) - length;
        best = length;
        if (id) *id = k->id;
    }
}
EI_REPLACE(0x004CF600, ClassifyToken);

const char* __fastcall FunctionSignature(int id, int type) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        if (k->id != id || !k->function) continue;
        switch (k->function[0]) {
        case 'F':
            if (type == kScriptFloat) return k->function + 1;
            break;
        case 'G':
            if (type == kScriptGroup) return k->function + 1;
            break;
        case 'O':
            if (type == kScriptObject) return k->function + 1;
            break;
        case 'S':
            if (type == kScriptString) return k->function + 1;
            break;
        }
    }
    return nullptr;
}
EI_REPLACE(0x004D0B10, FunctionSignature);

const char* __fastcall KeywordName(int id) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
        if (k->id == id) return k->name;
    return GameText(0x0078D848);   // "Unknown command"
}
EI_REPLACE(0x004D0BA0, KeywordName);

// ---- Tokens -------------------------------------------------------------------------------------

ScriptToken* ScriptToken::Construct() {
    new (&text) mfc::CString;
    text = GameText(0x0078BC6C);   // "None"
    line = -1;
    return this;
}
EI_REPLACE_METHOD(0x004CFB70, ScriptToken::Construct);

ScriptToken* ScriptToken::Assign(const ScriptToken& other) {
    text = other.text;
    line = other.line;
    return this;
}
EI_REPLACE_METHOD(0x004D0BD0, ScriptToken::Assign);

ScriptToken* ScriptToken::CopyConstruct(const ScriptToken& other) {
    new (&text) mfc::CString(other.text);
    line = other.line;
    return this;
}
EI_REPLACE_METHOD(0x004D1D30, ScriptToken::CopyConstruct);

void __stdcall ConstructTokens(ScriptToken* tokens, int count) {
    std::memset(tokens, 0, static_cast<unsigned>(count) * sizeof(ScriptToken));
    for (; count != 0; --count, ++tokens)
        if (tokens) tokens->Construct();
}
EI_REPLACE(0x004CF380, ConstructTokens);

// ---- Lexer --------------------------------------------------------------------------------------

BOOL CScript::Tokenize(const mfc::CString& source, ScriptTokenList* tokens) {
    const char* p = source;

    // RemoveAll.
    for (ScriptTokenList::Node* node = tokens->head; node; node = node->next) node->data.text.~CString();
    tokens->count = 0;
    tokens->free = nullptr;
    tokens->tail = nullptr;
    tokens->head = nullptr;
    mfc::PlexFreeDataChain(tokens->blocks);
    tokens->blocks = nullptr;

    const char quote = QuoteChar();
    int line = 1;
    while (*p) {
        while (IsSpace(*p)) {
            if (*p == '\n') ++line;
            ++p;
            if (!*p) break;
        }

        // A token runs to white space (a space inside quotes does not count) or to one of , ) ( : =
        // after its first character (not inside quotes). Keywords are split off afterwards.
        const char* start = p;
        int inQuote = 0;
        while (*p) {
            const char c = *p;
            if (IsSpace(c) && (c != ' ' || !inQuote)) break;
            if (c == quote) inQuote = !inQuote;
            if ((c == ',' || c == ')' || c == '(' || c == ':' || c == '=') && p != start && !inQuote) break;
            if (c == '\n') break;
            ++p;
        }
        int length = static_cast<int>(p - start);
        if (length > 0xFF) length = 0xFF;
        char buffer[0x100];
        gamecrt::strncpy(buffer, start, length);
        if (length == 0) continue;
        buffer[length] = 0;

        if (Strncmp(buffer, CommentOpen(), std::strlen(CommentOpen())) == 0) {
            // Block comment: the search for the end starts after the token (so "/**/" written as
            // one token does not end it) and skips quoted text.
            if (!*p) return Error(this, line, 1, GameText(0x0078D758), CommentClose());   // "End of file before '%s'"
            for (;;) {
                const char c = *p;
                if (c == '\n') ++line;
                if (c == quote) inQuote = !inQuote;
                if (!inQuote && Strncmp(p, CommentClose(), std::strlen(CommentClose())) == 0) {
                    p += std::strlen(CommentClose());
                    break;
                }
                ++p;
                if (!*p) return Error(this, line, 1, GameText(0x0078D758), CommentClose());
            }
            continue;
        }
        if (gamecrt::strnicmp(buffer, LineComment(), std::strlen(LineComment())) == 0) {
            while (*p && *p != '\n') ++p;
            continue;
        }

        // Keep only the part ClassifyToken recognises (a keyword prefix, a number, a quoted
        // string); the rest is scanned again as the next token.
        int rest;
        ClassifyAt(buffer, nullptr, &rest);
        p -= rest;
        mfc::CString text;
        reinterpret_cast<StringOps*>(&text)->InitN(buffer, static_cast<int>(std::strlen(buffer)) - rest);

        ScriptTokenList::Node* node = TakeNode(tokens);
        node->prev = tokens->tail;
        node->next = nullptr;
        ++tokens->count;
        std::memset(&node->data, 0, sizeof(node->data));
        node->data.Construct();
        node->data.text = text;
        node->data.line = line;
        if (tokens->tail) tokens->tail->next = node;
        else tokens->head = node;
        tokens->tail = node;
    }
    return 1;
}
EI_REPLACE_METHOD(0x004CF7A0, CScript::Tokenize);

BOOL CScript::ExpectLexeme(const ScriptTokenList* tokens, ScriptPos* pos, int id) {
    ScriptPos node = *pos;
    if (!node) return Error(this, tokens->tail->data.line, 1, GameText(0x0078D740));   // "Unexpected end of file"
    *pos = node->next;
    ScriptToken token;
    token.CopyConstruct(node->data);
    mfc::CString text(token.text);
    int found;
    ClassifyAt(text, &found, nullptr);
    if (found != id) return Error(this, token.line, 2, GameText(0x0078D730), static_cast<const char*>(text));   // "Wrong lexeme %s"
    return 1;
}
EI_REPLACE_METHOD(0x004CF4D0, CScript::ExpectLexeme);

// ---- Names --------------------------------------------------------------------------------------

mfc::CString* CScript::QualifyName(mfc::CString* out, const mfc::CString& name) {
    if (FileCount() == 0) {
        new (out) mfc::CString(name);
        return out;
    }
    mfc::CString qualified;
    qualified.Format(GameText(0x0078DA38), FileCount(), static_cast<const char*>(name));   // "!%i!%s"
    new (out) mfc::CString(qualified);
    return out;
}
EI_REPLACE_METHOD(0x004D37F0, CScript::QualifyName);

int CScript::FindScript(const char* name) {
    for (;;) {
        const MfcArray<ScriptDefinition>& scripts = Scripts();
        for (int i = 0; i < scripts.size; ++i)
            if (Mbscmp(scripts.data[i].name, name) == 0) return i;
        // Not found: retry without the leading "!<file>!" qualification.
        if (*name != '!') return -1;
        do {
            ++name;
            if (!*name) return -1;
        } while (*name != '!');
        ++name;
    }
}
EI_REPLACE_METHOD(0x004D3780, CScript::FindScript);

// ---- #define ------------------------------------------------------------------------------------

void __fastcall AddDefine(ScriptDefineList* defines, const ScriptDefine* define) {
    using Node = ScriptDefineList::Node;
    if (defines->head) {
        // Before the first define with a shorter name; when there is none, before the last one.
        const int length = Length(define->name);
        Node* before = defines->head;
        for (;;) {
            if (Length(before->data.name) < length || !before->next) break;
            before = before->next;
        }
        Node* prev = before->prev;
        Node* node = TakeNode(defines);
        node->prev = prev;
        node->next = before;
        ++defines->count;
        std::memset(&node->data, 0, sizeof(node->data));
        new (&node->data.name) mfc::CString;
        new (&node->data.value) mfc::CString;
        node->data.name = define->name;
        node->data.value = define->value;
        if (before->prev) before->prev->next = node;
        else defines->head = node;
        before->prev = node;
        return;
    }
    Node* tail = defines->tail;
    Node* node = TakeNode(defines);
    node->prev = tail;
    node->next = nullptr;
    ++defines->count;
    std::memset(&node->data, 0, sizeof(node->data));
    new (&node->data.name) mfc::CString;
    new (&node->data.value) mfc::CString;
    node->data.name = define->name;
    node->data.value = define->value;
    if (defines->tail) defines->tail->next = node;
    else defines->head = node;
    defines->tail = node;
}
EI_REPLACE(0x004D34F0, AddDefine);

void __fastcall ApplyDefines(mfc::CString* line, const ScriptDefineList* defines) {
    for (const ScriptDefineList::Node* node = defines->head; node;) {
        const ScriptDefineList::Node* next = node->next;
        // Replaces from the start again after each replacement (a value containing its own name
        // never ends; kept).
        for (;;) {
            const StringOps& text = Ops(*line);
            int at = text.Find(node->data.name);
            if (at < 0) break;
            mfc::CString right = text.Right(Length(*line) - Length(node->data.name) - at);
            mfc::CString left = text.Left(at);
            *line = (left + node->data.value) + right;
        }
        node = next;
    }
}
EI_REPLACE(0x004D33E0, ApplyDefines);
