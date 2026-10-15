// Record mode for the "script" family (script_vectors.h). Stolen bytes as in verify_script_parse.cpp.
// Every scenario loads scripts (the zone's and the interface's), so every one reaches the lexer.
#include "crt_vectors.h"
#include "script_vectors.h"
#include "vectors.h"

#include "../../src/game/script.h"

#include "ei/hook.h"

#include <cstring>
#include <string>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::script;

Sampler g_keywords("script", "@ScriptKeywords", kVariableSize, 1);
Sampler g_classify("script", "ClassifyToken", kVariableSize, 128, 4);
Sampler g_signature("script", "FunctionSignature", kVariableSize, 256);
Sampler g_keywordName("script", "KeywordName", kVariableSize, 256);
Sampler g_tokenize("script", "CScript::Tokenize", kVariableSize, 6, 3);
Sampler g_applyDefines("script", "ApplyDefines", kVariableSize, 128, 2);

} // namespace

// The keyword table and the lexer's data, once (with the game CRT's character classes, which the
// lexer reads too).
void ei::vectors::script::OfferFixtures() {
    static bool offered = false;
    if (offered) return;
    offered = true;
    crt::OfferCtype();
    Writer w;
    const ScriptKeyword* table = *reinterpret_cast<const ScriptKeyword* const*>(kKeywordTablePointer);
    std::uint32_t count = 0;
    while (table[count].name) ++count;
    w.Put(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table)));
    w.Put(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        w.Put<std::int32_t>(table[i].id);
        w.PutString(table[i].name);
        w.PutString(table[i].function);
        w.PutString(table[i].command);
    }
    w.Put(*reinterpret_cast<const std::uint8_t*>(kQuoteChar));
    w.Put(static_cast<std::uint32_t>(std::size(kStringPointers)));
    for (std::uintptr_t at : kStringPointers) {
        w.Put(static_cast<std::uint32_t>(at));
        w.PutString(*reinterpret_cast<const char* const*>(at));
    }
    w.Put(static_cast<std::uint32_t>(std::size(kTexts)));
    for (std::uintptr_t at : kTexts) {
        w.Put(static_cast<std::uint32_t>(at));
        w.PutString(reinterpret_cast<const char*>(at));
    }
    g_keywords.OfferBytes(w, ControlWord());
}

namespace {

// Tokens the lexer passes are at most 0xFF characters; longer texts (none known) are not kept.
constexpr std::size_t kMaxText = 0x400;

// 004CF600: SUB ESP,8; PUSH EBX; PUSH EBP (5). Bucket: number, string, no keyword, keyword.
void(__fastcall* s_classify)(const char*, int*, int*);
void __fastcall ClassifyRecord(const char* text, int* id, int* rest) {
    OfferFixtures();
    std::uint16_t cw = ControlWord();
    s_classify(text, id, rest);
    if (std::strlen(text) > kMaxText) return;
    Writer w;
    w.PutString(text);
    crt::PutNegativeClasses(w, text, std::strlen(text));
    w.Put<std::uint8_t>(static_cast<std::uint8_t>((id ? 1 : 0) | (rest ? 2 : 0)));
    w.Put<std::int32_t>(id ? *id : kUnasked);
    w.Put<std::int32_t>(rest ? *rest : kUnasked);
    unsigned bucket = 3;
    if (id) bucket = *id == kLexNumber ? 0 : *id == kLexString ? 1 : *id == 0 ? 2 : 3;
    g_classify.OfferBytes(w, cw, bucket);
}

// 004D0B10: MOV EAX,[0078BCA0] (5).
const char*(__fastcall* s_signature)(int, int);
const char* __fastcall SignatureRecord(int id, int type) {
    OfferFixtures();
    std::uint16_t cw = ControlWord();
    const char* result = s_signature(id, type);
    Writer w;
    w.Put<std::int32_t>(id);
    w.Put<std::int32_t>(type);
    w.PutString(result);
    g_signature.OfferBytes(w, cw);
    return result;
}

// 004D0BA0: MOV EDX,[0078BCA0] (6).
const char*(__fastcall* s_keywordName)(int);
const char* __fastcall KeywordNameRecord(int id) {
    OfferFixtures();
    std::uint16_t cw = ControlWord();
    const char* result = s_keywordName(id);
    Writer w;
    w.Put<std::int32_t>(id);
    w.PutString(result);
    g_keywordName.OfferBytes(w, cw);
    return result;
}

void PutCString(Writer& w, const mfc::CString& s) { w.PutString(static_cast<const char*>(s)); }

// 004CF7A0: PUSH -1; PUSH imm32 (7). Error results keep the script's error.
BOOL(__fastcall* s_tokenize)(CScript*, void*, const mfc::CString*, ScriptTokenList*);
BOOL __fastcall TokenizeRecord(CScript* self, void*, const mfc::CString* source, ScriptTokenList* tokens) {
    OfferFixtures();
    std::uint16_t cw = ControlWord();
    BOOL result = s_tokenize(self, nullptr, source, tokens);
    const std::uint32_t length = static_cast<std::uint32_t>(source->GetLength());
    if (length > kMaxSource) return result;
    Writer w;
    w.Put(length);
    w.PutBytes(static_cast<const char*>(*source), length);
    crt::PutNegativeClasses(w, static_cast<const char*>(*source), length);
    w.Put<std::int32_t>(tokens->blockSize);
    w.Put<std::int32_t>(result);
    if (!result) {
        w.Put<std::int32_t>(self->ErrorCode());
        w.Put<std::int32_t>(self->ErrorLine());
        PutCString(w, self->ErrorText());
    }
    w.Put(static_cast<std::uint32_t>(tokens->count));
    std::uint64_t all = 0xCBF29CE484222325ull;
    for (const ScriptTokenList::Node* node = tokens->head; node; node = node->next)
        all = TokenHash(node->data.text, node->data.line, all);
    w.Put(all);
    for (const ScriptTokenList::Node* node = tokens->head; node; node = node->next)
        w.Put(static_cast<std::uint32_t>(TokenHash(node->data.text, node->data.line)));
    g_tokenize.OfferBytes(w, cw, SourceBucket(length));
    return result;
}

// 004D33E0: MOV EAX,FS:[0] (6). Bucket 1: the line changed.
void(__fastcall* s_applyDefines)(mfc::CString*, const ScriptDefineList*);
void __fastcall ApplyDefinesRecord(mfc::CString* line, const ScriptDefineList* defines) {
    OfferFixtures();
    Writer w;
    PutCString(w, *line);
    w.Put(static_cast<std::uint32_t>(defines->count));
    for (const ScriptDefineList::Node* node = defines->head; node; node = node->next) {
        PutCString(w, node->data.name);
        PutCString(w, node->data.value);
    }
    const std::string before = static_cast<const char*>(*line);
    std::uint16_t cw = ControlWord();
    s_applyDefines(line, defines);
    PutCString(w, *line);
    g_applyDefines.OfferBytes(w, cw, before == static_cast<const char*>(*line) ? 0 : 1);
}

} // namespace

EI_RECORD(0x004CF600, ClassifyRecord, 5, s_classify);
EI_RECORD(0x004D0B10, SignatureRecord, 5, s_signature);
EI_RECORD(0x004D0BA0, KeywordNameRecord, 6, s_keywordName);
EI_RECORD(0x004CF7A0, TokenizeRecord, 7, s_tokenize);
EI_RECORD(0x004D33E0, ApplyDefinesRecord, 6, s_applyDefines);
