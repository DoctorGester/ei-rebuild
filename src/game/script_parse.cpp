// Script loader, part 1: tokens, the lexer (CScript::Tokenize), keyword lookup, #define handling
// and name helpers. The parser proper is in script_parse_load.cpp. See script.h and
// docs/notes/script.md ("Loader and parser").
// Rebirth patches the keyword lookups.
#include "script.h"
#include "game_strings.h"
#include "script_parse.h"
#include "script_addon.h"
#include "game_data.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdarg>

#include <cstring>
#include <memory>
#include <new>
#include <string>

// ---- Game CRT and MFC helpers (local to the loader) ---------------------------------------------

namespace scriptparse {

int IsDigit(int c) { return text::IsDigit(c); }
int IsSpace(int c) { return text::IsSpace(c); }
int Mbscmp(std::string_view a0, std::string_view a1) { return str::Compare(a0, a1); }

} // namespace scriptparse

using namespace scriptparse;



int scriptparse::ScriptErrorV(CScript* script, int line, int code, const char* format, va_list args) {
    char text[0x400];
    gameapi::wvsprintfA(text, format, args);
    script->ErrorLine() = line;
    script->ErrorText() = text;
    script->ErrorCode() = code;
    return 0;
}

// Rebirth's AddNewScriptCommands also copies the error to addon.log (not reproduced).

// ---- Keyword table ------------------------------------------------------------------------------

// The game's table (script.h): the punctuation and section keywords, the commands, the functions.
const ScriptKeyword kScriptKeywords[226] = /*0078BCA0*/ {
    {"(", 109, nullptr, nullptr},
    {")", 110, nullptr, nullptr},
    {",", 113, nullptr, nullptr},
    {":", 115, nullptr, nullptr},
    {"=", 114, nullptr, nullptr},
    {"WorldScript", 116, nullptr, nullptr},
    {"Script", 117, nullptr, nullptr},
    {"DeclareScript", 118, nullptr, nullptr},
    {"GlobalVars", 119, nullptr, nullptr},
    {"if", 111, nullptr, nullptr},
    {"then", 112, nullptr, nullptr},
    {"ActivateTrap", 149, nullptr, "of"},
    {"AddLoot", 224, nullptr, "fss"},
    {"AddRectToArea", 195, nullptr, "fffff"},
    {"AddRoundToArea", 196, nullptr, "ffff"},
    {"AddMob", 160, nullptr, "s"},
    {"AddObject", 34, nullptr, "go"},
    {"AddUnitToParty", 134, nullptr, "fss"},
    {"AddUnitToServer", 136, nullptr, "sssfff"},
    {"AddUnitUnderControl", 131, nullptr, "fo"},
    {"AttachParticleSource", 18, nullptr, "ff"},
    {"AttachParticles", 231, nullptr, "fo"},
    {"Attack", 60, nullptr, "oo"},
    {"BlockUnit", 50, nullptr, "of"},
    {"Cast", 61, nullptr, "off"},
    {"CastSpellPoint", 168, nullptr, "sffff"},
    {"CastSpellUnit", 169, nullptr, "sffo"},
    {"ConsoleFloat", 125, nullptr, "f"},
    {"ConsoleString", 126, nullptr, "s"},
    {"CopyItems", 212, nullptr, "fss"},
    {"CopyLoot", 203, nullptr, "fss"},
    {"CopyStats", 157, nullptr, "fss"},
    {"Crawl", 62, nullptr, "o"},
    {"CreateFX", 23, nullptr, "fffffs"},
    {"CreateFXSource", 22, nullptr, "ffffffs"},
    {"CreateLightning", 15, nullptr, "ffffffff"},
    {"CreateParticleSource", 14, nullptr, "fffffs"},
    {"CreateParty", 154, nullptr, "fs"},
    {"CreatePointLight", 11, nullptr, "ffffffff"},
    {"CreateRandomizedFXSource", 24, nullptr, "fffffffffs"},
    {"DeleteArea", 198, nullptr, "f"},
    {"DeleteFXSource", 25, nullptr, "f"},
    {"DeleteLightning", 26, nullptr, "f"},
    {"DeleteParticleSource", 17, nullptr, "f"},
    {"DeletePointLight", 13, nullptr, "f"},
    {"Follow", 59, nullptr, "oo"},
    {"EnableLever", 204, nullptr, "of"},
    {"EraseQuestItem", 147, nullptr, "ff"},
    {"FixItems", 223, nullptr, ""},
    {"FixWorldTime", 183, nullptr, "f"},
    {"GSSetVar", 127, nullptr, "fsf"},
    {"GSSetVarMax", 152, nullptr, "fsf"},
    {"GSDelVar", 130, nullptr, "fs"},
    {"GiveDexterity", 207, nullptr, "of"},
    {"GiveIntelligence", 208, nullptr, "of"},
    {"GiveItem", 166, nullptr, "fs"},
    {"GiveMoney", 167, nullptr, "ff"},
    {"GiveQuestItem", 146, nullptr, "fs"},
    {"GiveSkill", 205, nullptr, "osf"},
    {"GiveStrength", 206, nullptr, "of"},
    {"GiveUnitQuestItem", 175, nullptr, "os"},
    {"GiveUnitSpell", 179, nullptr, "os"},
    {"GodMode", 151, nullptr, "ff"},
    {"Guard", 51, nullptr, "offf"},
    {"HideObject", 194, nullptr, "of"},
    {"Idle", 52, nullptr, "o"},
    {"InvokeAlarm", 67, nullptr, "fff"},
    {"InflictDamage", 68, nullptr, "of"},
    {"KillScript", 121, nullptr, ""},
    {"KillUnit", 53, nullptr, "o"},
    {"LeaveToZone", 138, nullptr, "fsf"},
    {"Lie", 201, nullptr, "o"},
    {"MoveParticleSource", 16, nullptr, "ffffff"},
    {"MovePointLight", 12, nullptr, "ffffff"},
    {"MoveToPoint", 2, nullptr, "off"},
    {"MoveToObject", 4, nullptr, "oo"},
    {"PlayAnimation", 27, nullptr, "os"},
    {"PlayFX", 28, nullptr, "ffffs"},
    {"PlayCamera", 161, nullptr, "s"},
    {"PlayMovie", 193, nullptr, "s"},
    {"PlayMusic", 29, nullptr, "fs"},
    {"SetPlayerAggression", 164, nullptr, "ff"},
    {"QStart", 213, nullptr, "s"},
    {"QFinish", 214, nullptr, ""},
    {"QObjArea", 215, nullptr, "f"},
    {"QObjSeeObject", 216, nullptr, "s"},
    {"QObjSeeUnit", 217, nullptr, "s"},
    {"QObjKillUnit", 218, nullptr, "s"},
    {"QObjKillGroup", 219, nullptr, "s"},
    {"QObjUse", 220, nullptr, "sf"},
    {"QObjGetItem", 221, nullptr, "f"},
    {"QuestComplete", 159, nullptr, "fs"},
    {"RecalcMercBriefings", 210, nullptr, ""},
    {"RedeployParty", 211, nullptr, "f"},
    {"RemoveParty", 155, nullptr, "fs"},
    {"RemoveObject", 35, nullptr, "go"},
    {"RemoveObjectFromServer", 150, nullptr, "o"},
    {"RemoveQuestItem", 174, nullptr, "of"},
    {"RemoveUnitFromControl", 132, nullptr, "fo"},
    {"RemoveUnitFromParty", 135, nullptr, "fs"},
    {"RemoveUnitFromServer", 137, nullptr, "o"},
    {"ResetTarget", 232, nullptr, "o"},
    {"Rest", 176, nullptr, "o"},
    {"RotateTo", 3, nullptr, "off"},
    {"Run", 65, nullptr, "o"},
    {"RunWorldTime", 182, nullptr, "f"},
    {"SendEvent", 57, nullptr, "ff"},
    {"SendStringEvent", 58, nullptr, "fs"},
    {"Sentry", 148, nullptr, "off"},
    {"SetBackGroundColor", 31, nullptr, "fff"},
    {"SetCameraOrientation", 20, nullptr, "f"},
    {"SetCameraPosition", 19, nullptr, "fff"},
    {"SetCP", 142, nullptr, "offf"},
    {"SetCPFast", 173, nullptr, "offf"},
    {"SetCurrentParty", 156, nullptr, "fs"},
    {"SetDiplomacy", 55, nullptr, "fff"},
    {"SetDirectionToObject", 6, nullptr, "f"},
    {"SetEnemy", 56, nullptr, "of"},
    {"SetParticleSourceSize", 186, nullptr, "ff"},
    {"SetPlayer", 54, nullptr, "of"},
    {"SetScience", 172, nullptr, "offffff"},
    {"SetSpellAggression", 66, nullptr, "of"},
    {"SetSunLight", 1, nullptr, "fffffffff"},
    {"SetWaterLevel", 184, nullptr, "fff"},
    {"SetWind", 30, nullptr, "ffff"},
    {"Sleep", 7, nullptr, "f"},
    {"SleepUntil", 222, nullptr, "f"},
    {"SleepUntilIdle", 185, nullptr, "o"},
    {"Stand", 63, nullptr, "o"},
    {"StartAnimation", 5, nullptr, "os"},
    {"ShowCredits", 163, nullptr, "s"},
    {"ShowBitmap", 33, nullptr, "fs"},
    {"SwitchLeverState", 145, nullptr, "of"},
    {"SwitchLeverStateEx", 158, nullptr, "off"},
    {"UMAg", 200, nullptr, "of"},
    {"UMAggression", 45, nullptr, "o"},
    {"UMClear", 36, nullptr, "o"},
    {"UMCorpseWatcher", 48, nullptr, "o"},
    {"UMFear", 199, nullptr, "of"},
    {"UMFollow", 202, nullptr, "oo"},
    {"UMGuard", 38, nullptr, "offf"},
    {"UMGuardEx", 39, nullptr, "offff"},
    {"UMPatrol", 40, nullptr, "o"},
    {"UMPatrolAddPoint", 42, nullptr, "off"},
    {"UMPatrolAddPointLook", 43, nullptr, "offf"},
    {"UMPatrolClear", 41, nullptr, "o"},
    {"UMPlayer", 49, nullptr, "o"},
    {"UMRevenge", 46, nullptr, "o"},
    {"UMSentry", 37, nullptr, "off"},
    {"UMStandard", 44, nullptr, "o"},
    {"UMSuspection", 47, nullptr, "o"},
    {"WaitEndAnimation", 10, nullptr, ""},
    {"WaitSegment", 9, nullptr, "f"},
    {"Walk", 64, nullptr, "o"},
    {"For", 107, nullptr, "Og"},
    {"ForIf", 108, nullptr, "Ogf"},
    {"Every", 104, "FOgf", nullptr},
    {"Any", 105, "FOgf", nullptr},
    {"Sum", 106, "FOgf", nullptr},
    {"IsEqual", 69, "Fff", nullptr},
    {"IsLess", 70, "Fff", nullptr},
    {"IsGreater", 71, "Fff", nullptr},
    {"IsEqualString", 72, "Fss", nullptr},
    {"Add", 73, "Fff", nullptr},
    {"Sub", 74, "Fff", nullptr},
    {"Mul", 75, "Fff", nullptr},
    {"Div", 76, "Fff", nullptr},
    {"Not", 77, "Ff", nullptr},
    {"HaveItem", 124, "Fff", nullptr},
    {"GetObject", 78, "Of", nullptr},
    {"GetObjectByName", 133, "Os", nullptr},
    {"GetObjectByID", 171, "Os", nullptr},
    {"IsInSquare", 187, "Fffffff", nullptr},
    {"UnitInSquare", 188, "Foffff", nullptr},
    {"GetMercsNumber", 209, "Ff", nullptr},
    {"GetZValue", 189, "Fff", nullptr},
    {"GetBSZValue", 190, "Fff", nullptr},
    {"HP", 79, "Fo", nullptr},
    {"MaxHP", 80, "Fo", nullptr},
    {"Mana", 81, "Fo", nullptr},
    {"MaxMana", 82, "Fo", nullptr},
    {"DistanceUnitUnit", 83, "Foo", nullptr},
    {"DistanceUnitPoint", 84, "Foff", nullptr},
    {"Random", 95, "Ff", nullptr},
    {"GetLeverState", 144, "Fo", nullptr},
    {"GetX", 85, "Fo", nullptr},
    {"GetY", 86, "Fo", nullptr},
    {"GetZ", 87, "Fo", nullptr},
    {"GetFutureX", 191, "Fof", nullptr},
    {"GetFutureY", 192, "Fof", nullptr},
    {"GroupSize", 88, "Fg", nullptr},
    {"GroupAdd", 89, "Ggg", nullptr},
    {"GroupSub", 90, "Ggg", nullptr},
    {"GroupCross", 91, "Ggg", nullptr},
    {"GroupHas", 92, "Fgo", nullptr},
    {"GSGetVar", 128, "Ffs", nullptr},
    {"IsUnitInWater", 153, "Fo", nullptr},
    {"UnitSee", 93, "Go", nullptr},
    {"GroupSee", 94, "Gg", nullptr},
    {"PlayerSee", 165, "Gf", nullptr},
    {"GetDiplomacy", 96, "Fff", nullptr},
    {"IsEnemy", 97, "Fof", nullptr},
    {"IsAlive", 99, "Fo", nullptr},
    {"IsCameraPlaying", 162, "F", nullptr},
    {"IsDead", 100, "Fo", nullptr},
    {"IsPlayerInDanger", 177, "Ff", nullptr},
    {"IsPlayerInSafety", 178, "Ff", nullptr},
    {"GetPlayerUnits", 98, "Gf", nullptr},
    {"GetUnitOfPlayer", 180, "Off", nullptr},
    {"GetPlayer", 101, "Fo", nullptr},
    {"GetObjectID", 102, "Fo", nullptr},
    {"GetAIClass", 143, "Fo", nullptr},
    {"GetLootItemsCount", 225, "Ff", nullptr},
    {"GetMoney", 226, "Ff", nullptr},
    {"IsAlarm", 103, "Ff", nullptr},
    {"AlarmTime", 139, "Ff", nullptr},
    {"AlarmPosX", 140, "Ff", nullptr},
    {"AlarmPosY", 141, "Ff", nullptr},
    {"IsNight", 170, "F", nullptr},
    {"GetWorldTime", 181, "F", nullptr},
    {"IsInArea", 197, "Ffff", nullptr},
    {"WasLooted", 227, "Fo", nullptr},
    {"IsUnitVisible", 229, "Fo", nullptr},
    {"GetLeader", 228, "O", nullptr},
    {"IsUnitBlocked", 230, "Fo", nullptr},
    {nullptr, 0, nullptr, nullptr}
};

const ScriptKeyword* KeywordTable() {
    // Rebirth's AddNewScriptCommands points the classifier at its extended table. With the flag on,
    // the extended table is ours (script_addon.cpp, built from this one); else this one.
    if (scriptaddon::Enabled()) return scriptaddon::Table();
    return kScriptKeywords;
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
    const char quote = kQuote;
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
    const bool addon = scriptaddon::Enabled();
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        int length = static_cast<int>(std::strlen(k->name));
        if (length < best) continue;
        if (text::CompareIgnoringCase(p, k->name, static_cast<std::size_t>(length)) != 0) continue;
        best = length;
        // Rebirth's AddNewScriptCommands: an identifier-like match must end the identifier.
        if (addon && scriptaddon::RejectsMatch(k->name, p[length])) continue;
        if (rest) *rest = static_cast<int>(std::strlen(p)) - length;
        if (id) *id = k->id;
    }
}

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

const char* __fastcall KeywordName(int id) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
        if (k->id == id) return k->name;
    return "Unknown command";
}

ScriptToken* ScriptToken::Construct() {
    std::construct_at(&text);
    text = "None";
    line = -1;
    return this;
}

ScriptToken* ScriptToken::Assign(const ScriptToken& other) {
    text = other.text;
    line = other.line;
    return this;
}

ScriptToken* ScriptToken::CopyConstruct(const ScriptToken& other) {
    std::construct_at(&text, other.text);
    line = other.line;
    return this;
}

void ConstructTokens(ScriptToken* tokens, int count) {
    std::memset(tokens, 0, static_cast<unsigned>(count) * sizeof(ScriptToken));
    for (; count != 0; --count, ++tokens)
        if (tokens) tokens->Construct();
}

BOOL CScript::Tokenize(const std::string& source, ScriptTokenList* tokens) {
    const char* p = source.c_str();

    // RemoveAll.
    for (ScriptTokenList::Node* node = tokens->head; node; node = node->next) std::destroy_at(&node->data.text);
    tokens->count = 0;
    tokens->free = nullptr;
    tokens->tail = nullptr;
    tokens->head = nullptr;
    mfc::PlexFreeDataChain(tokens->blocks);
    tokens->blocks = nullptr;

    const char quote = kQuote;
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
        #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
        std::strncpy(buffer, start, length);
        if (length == 0) continue;
        buffer[length] = 0;

        if (std::strncmp(buffer, kCommentOpen, std::strlen(kCommentOpen)) == 0) {
            // Block comment: the search for the end starts after the token (so "/**/" written as
            // one token does not end it) and skips quoted text.
            if (!*p) return Error(this, line, 1, "End of file before '%s'", kCommentClose);
            for (;;) {
                const char c = *p;
                if (c == '\n') ++line;
                if (c == quote) inQuote = !inQuote;
                if (!inQuote && std::strncmp(p, kCommentClose, std::strlen(kCommentClose)) == 0) {
                    p += std::strlen(kCommentClose);
                    break;
                }
                ++p;
                if (!*p) return Error(this, line, 1, "End of file before '%s'", kCommentClose);
            }
            continue;
        }
        if (text::CompareIgnoringCase(buffer, kLineComment, std::strlen(kLineComment)) == 0) {
            while (*p && *p != '\n') ++p;
            continue;
        }

        // Keep only the part ClassifyToken recognises (a keyword prefix, a number, a quoted
        // string); the rest is scanned again as the next token.
        int rest;
        ClassifyToken(buffer, nullptr, &rest);
        p -= rest;
        std::string text(buffer, static_cast<int>(std::strlen(buffer)) - rest);

        // CList::AddTail as the game inlines it: the new node's token zeroed, then constructed.
        ScriptTokenList::Node* node = tokens->NewNode(tokens->tail, nullptr);
        node->data.Construct();
        node->data.text = text;
        node->data.line = line;
        if (tokens->tail) tokens->tail->next = node;
        else tokens->head = node;
        tokens->tail = node;
    }
    return 1;
}

BOOL CScript::ExpectLexeme(const ScriptTokenList* tokens, ScriptPos* pos, int id) {
    ScriptPos node = *pos;
    if (!node) return Error(this, tokens->tail->data.line, 1, "Unexpected end of file");
    *pos = node->next;
    ScriptToken token;
    token.CopyConstruct(node->data);
    std::string text = token.text;
    int found;
    ClassifyToken(text.c_str(), &found, nullptr);
    if (found != id) return Error(this, token.line, 2, "Wrong lexeme %s", text.c_str());
    return 1;
}

std::string* CScript::QualifyName(std::string* out, const std::string& name) {
    if (FileCount() == 0) {
        *out = name;
        return out;
    }
    std::string qualified;
    qualified = str::Format("!%i!%s", FileCount(), name.c_str());
    *out = qualified;
    return out;
}

int CScript::FindScript(const char* name) {
    for (;;) {
        const mfc::CArray<ScriptDefinition>& scripts = Scripts();
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

// ---- #define ------------------------------------------------------------------------------------

void AddDefine(ScriptDefineList* defines, const ScriptDefine* define) {
    using Node = ScriptDefineList::Node;
    if (defines->head) {
        // Before the first define with a shorter name; when there is none, before the last one.
        const int length = static_cast<int>(define->name.size());
        Node* before = defines->head;
        for (;;) {
            if (static_cast<int>(before->data.name.size()) < length || !before->next) break;
            before = before->next;
        }
        Node* prev = before->prev;
        Node* node = defines->NewNode(prev, before);   // the strings constructed empty
        node->data.name = define->name;
        node->data.value = define->value;
        if (before->prev) before->prev->next = node;
        else defines->head = node;
        before->prev = node;
        return;
    }
    Node* tail = defines->tail;
    Node* node = defines->NewNode(tail, nullptr);
    node->data.name = define->name;
    node->data.value = define->value;
    if (defines->tail) defines->tail->next = node;
    else defines->head = node;
    defines->tail = node;
}

void __fastcall ApplyDefines(std::string* line, const ScriptDefineList* defines) {
    for (const ScriptDefineList::Node* node = defines->head; node;) {
        const ScriptDefineList::Node* next = node->next;
        // Replaces from the start again after each replacement (a value containing its own name
        // never ends; kept).
        for (;;) {
            const std::string& text = *line;
            int at = str::Find(text, node->data.name.c_str());
            if (at < 0) break;
            std::string right = str::Right(text, static_cast<int>(line->size()) - static_cast<int>(node->data.name.size()) - at);
            std::string left = str::Left(text, at);
            *line = (left + node->data.value) + right;
        }
        node = next;
    }
}
