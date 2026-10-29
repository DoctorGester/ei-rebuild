// Script loader, part 1: tokens, the lexer (CScript::Tokenize), keyword lookup, #define handling
// and name helpers. The parser proper is in script_parse_load.cpp. See script.h and
// docs/notes/script.md ("Loader and parser").
// Rebirth patches the keyword lookups.
#include "script.h"
#include "script_parse.h"
#include "script_addon.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdarg>

#include <cstring>
#include <memory>
#include <new>
#include <string>

// ---- Game CRT and library helpers (local to the loader) ---------------------------------------------

namespace scriptparse {

bool IsDigit(int c) { return text::IsDigit(c); }
bool IsSpace(int c) { return text::IsSpace(c); }
int Mbscmp(std::string_view a0, std::string_view a1) { return str::Compare(a0, a1); }

} // namespace scriptparse

using namespace scriptparse;



int scriptparse::ScriptErrorV(CScript* script, int line, int code, const char* format, va_list args) {
    char text[0x400];
    gameapi::wvsprintfA(text, format, args);
    script->errorLine_ = line;
    script->errorText_ = text;
    script->errorCode_ = code;
    return 0;
}

// Rebirth's AddNewScriptCommands also copies the error to addon.log (not reproduced).

// ---- Keyword table ------------------------------------------------------------------------------

constexpr ScriptFunction Lex(ScriptLexeme lexeme) { return static_cast<ScriptFunction>(lexeme); }

// The game's table (script.h): the punctuation and section keywords, the commands, the functions.
const ScriptKeyword kScriptKeywords[226] = /*0078BCA0*/ {
    {"(", Lex(kLexOpen), nullptr, nullptr},
    {")", Lex(kLexClose), nullptr, nullptr},
    {",", Lex(kLexComma), nullptr, nullptr},
    {":", Lex(kLexColon), nullptr, nullptr},
    {"=", Lex(kLexAssign), nullptr, nullptr},
    {"WorldScript", Lex(kLexWorldScript), nullptr, nullptr},
    {"Script", Lex(kLexScript), nullptr, nullptr},
    {"DeclareScript", Lex(kLexDeclareScript), nullptr, nullptr},
    {"GlobalVars", Lex(kLexGlobalVars), nullptr, nullptr},
    {"if", Lex(kLexIf), nullptr, nullptr},
    {"then", Lex(kLexThen), nullptr, nullptr},
    {"ActivateTrap", ScriptFunction::ActivateTrap, nullptr, "of"},
    {"AddLoot", ScriptFunction::AddLoot, nullptr, "fss"},
    {"AddRectToArea", ScriptFunction::AddRectToArea, nullptr, "fffff"},
    {"AddRoundToArea", ScriptFunction::AddRoundToArea, nullptr, "ffff"},
    {"AddMob", ScriptFunction::AddMob, nullptr, "s"},
    {"AddObject", ScriptFunction::AddObject, nullptr, "go"},
    {"AddUnitToParty", ScriptFunction::AddUnitToParty, nullptr, "fss"},
    {"AddUnitToServer", ScriptFunction::AddUnitToServer, nullptr, "sssfff"},
    {"AddUnitUnderControl", ScriptFunction::AddUnitUnderControl, nullptr, "fo"},
    {"AttachParticleSource", ScriptFunction::AttachParticleSource, nullptr, "ff"},
    {"AttachParticles", ScriptFunction::AttachParticles, nullptr, "fo"},
    {"Attack", ScriptFunction::Attack, nullptr, "oo"},
    {"BlockUnit", ScriptFunction::BlockUnit, nullptr, "of"},
    {"Cast", ScriptFunction::Cast, nullptr, "off"},
    {"CastSpellPoint", ScriptFunction::CastSpellPoint, nullptr, "sffff"},
    {"CastSpellUnit", ScriptFunction::CastSpellUnit, nullptr, "sffo"},
    {"ConsoleFloat", ScriptFunction::ConsoleFloat, nullptr, "f"},
    {"ConsoleString", ScriptFunction::ConsoleString, nullptr, "s"},
    {"CopyItems", ScriptFunction::CopyItems, nullptr, "fss"},
    {"CopyLoot", ScriptFunction::CopyLoot, nullptr, "fss"},
    {"CopyStats", ScriptFunction::CopyStats, nullptr, "fss"},
    {"Crawl", ScriptFunction::Crawl, nullptr, "o"},
    {"CreateFX", ScriptFunction::CreateFX, nullptr, "fffffs"},
    {"CreateFXSource", ScriptFunction::CreateFXSource, nullptr, "ffffffs"},
    {"CreateLightning", ScriptFunction::CreateLightning, nullptr, "ffffffff"},
    {"CreateParticleSource", ScriptFunction::CreateParticleSource, nullptr, "fffffs"},
    {"CreateParty", ScriptFunction::CreateParty, nullptr, "fs"},
    {"CreatePointLight", ScriptFunction::CreatePointLight, nullptr, "ffffffff"},
    {"CreateRandomizedFXSource", ScriptFunction::CreateRandomizedFXSource, nullptr, "fffffffffs"},
    {"DeleteArea", ScriptFunction::DeleteArea, nullptr, "f"},
    {"DeleteFXSource", ScriptFunction::DeleteFXSource, nullptr, "f"},
    {"DeleteLightning", ScriptFunction::DeleteLightning, nullptr, "f"},
    {"DeleteParticleSource", ScriptFunction::DeleteParticleSource, nullptr, "f"},
    {"DeletePointLight", ScriptFunction::DeletePointLight, nullptr, "f"},
    {"Follow", ScriptFunction::Follow, nullptr, "oo"},
    {"EnableLever", ScriptFunction::EnableLever, nullptr, "of"},
    {"EraseQuestItem", ScriptFunction::EraseQuestItem, nullptr, "ff"},
    {"FixItems", ScriptFunction::FixItems, nullptr, ""},
    {"FixWorldTime", ScriptFunction::FixWorldTime, nullptr, "f"},
    {"GSSetVar", ScriptFunction::GSSetVar, nullptr, "fsf"},
    {"GSSetVarMax", ScriptFunction::GSSetVarMax, nullptr, "fsf"},
    {"GSDelVar", ScriptFunction::GSDelVar, nullptr, "fs"},
    {"GiveDexterity", ScriptFunction::GiveDexterity, nullptr, "of"},
    {"GiveIntelligence", ScriptFunction::GiveIntelligence, nullptr, "of"},
    {"GiveItem", ScriptFunction::GiveItem, nullptr, "fs"},
    {"GiveMoney", ScriptFunction::GiveMoney, nullptr, "ff"},
    {"GiveQuestItem", ScriptFunction::GiveQuestItem, nullptr, "fs"},
    {"GiveSkill", ScriptFunction::GiveSkill, nullptr, "osf"},
    {"GiveStrength", ScriptFunction::GiveStrength, nullptr, "of"},
    {"GiveUnitQuestItem", ScriptFunction::GiveUnitQuestItem, nullptr, "os"},
    {"GiveUnitSpell", ScriptFunction::GiveUnitSpell, nullptr, "os"},
    {"GodMode", ScriptFunction::GodMode, nullptr, "ff"},
    {"Guard", ScriptFunction::Guard, nullptr, "offf"},
    {"HideObject", ScriptFunction::HideObject, nullptr, "of"},
    {"Idle", ScriptFunction::Idle, nullptr, "o"},
    {"InvokeAlarm", ScriptFunction::InvokeAlarm, nullptr, "fff"},
    {"InflictDamage", ScriptFunction::InflictDamage, nullptr, "of"},
    {"KillScript", ScriptFunction::KillScript, nullptr, ""},
    {"KillUnit", ScriptFunction::KillUnit, nullptr, "o"},
    {"LeaveToZone", ScriptFunction::LeaveToZone, nullptr, "fsf"},
    {"Lie", ScriptFunction::Lie, nullptr, "o"},
    {"MoveParticleSource", ScriptFunction::MoveParticleSource, nullptr, "ffffff"},
    {"MovePointLight", ScriptFunction::MovePointLight, nullptr, "ffffff"},
    {"MoveToPoint", ScriptFunction::MoveToPoint, nullptr, "off"},
    {"MoveToObject", ScriptFunction::MoveToObject, nullptr, "oo"},
    {"PlayAnimation", ScriptFunction::PlayAnimation, nullptr, "os"},
    {"PlayFX", ScriptFunction::PlayFX, nullptr, "ffffs"},
    {"PlayCamera", ScriptFunction::PlayCamera, nullptr, "s"},
    {"PlayMovie", ScriptFunction::PlayMovie, nullptr, "s"},
    {"PlayMusic", ScriptFunction::PlayMusic, nullptr, "fs"},
    {"SetPlayerAggression", ScriptFunction::SetPlayerAggression, nullptr, "ff"},
    {"QStart", ScriptFunction::QStart, nullptr, "s"},
    {"QFinish", ScriptFunction::QFinish, nullptr, ""},
    {"QObjArea", ScriptFunction::QObjArea, nullptr, "f"},
    {"QObjSeeObject", ScriptFunction::QObjSeeObject, nullptr, "s"},
    {"QObjSeeUnit", ScriptFunction::QObjSeeUnit, nullptr, "s"},
    {"QObjKillUnit", ScriptFunction::QObjKillUnit, nullptr, "s"},
    {"QObjKillGroup", ScriptFunction::QObjKillGroup, nullptr, "s"},
    {"QObjUse", ScriptFunction::QObjUse, nullptr, "sf"},
    {"QObjGetItem", ScriptFunction::QObjGetItem, nullptr, "f"},
    {"QuestComplete", ScriptFunction::QuestComplete, nullptr, "fs"},
    {"RecalcMercBriefings", ScriptFunction::RecalcMercBriefings, nullptr, ""},
    {"RedeployParty", ScriptFunction::RedeployParty, nullptr, "f"},
    {"RemoveParty", ScriptFunction::RemoveParty, nullptr, "fs"},
    {"RemoveObject", ScriptFunction::RemoveObject, nullptr, "go"},
    {"RemoveObjectFromServer", ScriptFunction::RemoveObjectFromServer, nullptr, "o"},
    {"RemoveQuestItem", ScriptFunction::RemoveQuestItem, nullptr, "of"},
    {"RemoveUnitFromControl", ScriptFunction::RemoveUnitFromControl, nullptr, "fo"},
    {"RemoveUnitFromParty", ScriptFunction::RemoveUnitFromParty, nullptr, "fs"},
    {"RemoveUnitFromServer", ScriptFunction::RemoveUnitFromServer, nullptr, "o"},
    {"ResetTarget", ScriptFunction::ResetTarget, nullptr, "o"},
    {"Rest", ScriptFunction::Rest, nullptr, "o"},
    {"RotateTo", ScriptFunction::RotateTo, nullptr, "off"},
    {"Run", ScriptFunction::Run, nullptr, "o"},
    {"RunWorldTime", ScriptFunction::RunWorldTime, nullptr, "f"},
    {"SendEvent", ScriptFunction::SendEvent, nullptr, "ff"},
    {"SendStringEvent", ScriptFunction::SendStringEvent, nullptr, "fs"},
    {"Sentry", ScriptFunction::Sentry, nullptr, "off"},
    {"SetBackGroundColor", ScriptFunction::SetBackGroundColor, nullptr, "fff"},
    {"SetCameraOrientation", ScriptFunction::SetCameraOrientation, nullptr, "f"},
    {"SetCameraPosition", ScriptFunction::SetCameraPosition, nullptr, "fff"},
    {"SetCP", ScriptFunction::SetCP, nullptr, "offf"},
    {"SetCPFast", ScriptFunction::SetCPFast, nullptr, "offf"},
    {"SetCurrentParty", ScriptFunction::SetCurrentParty, nullptr, "fs"},
    {"SetDiplomacy", ScriptFunction::SetDiplomacy, nullptr, "fff"},
    {"SetDirectionToObject", ScriptFunction::SetDirectionToObject, nullptr, "f"},
    {"SetEnemy", ScriptFunction::SetEnemy, nullptr, "of"},
    {"SetParticleSourceSize", ScriptFunction::SetParticleSourceSize, nullptr, "ff"},
    {"SetPlayer", ScriptFunction::SetPlayer, nullptr, "of"},
    {"SetScience", ScriptFunction::SetScience, nullptr, "offffff"},
    {"SetSpellAggression", ScriptFunction::SetSpellAggression, nullptr, "of"},
    {"SetSunLight", ScriptFunction::SetSunLight, nullptr, "fffffffff"},
    {"SetWaterLevel", ScriptFunction::SetWaterLevel, nullptr, "fff"},
    {"SetWind", ScriptFunction::SetWind, nullptr, "ffff"},
    {"Sleep", ScriptFunction::Sleep, nullptr, "f"},
    {"SleepUntil", ScriptFunction::SleepUntil, nullptr, "f"},
    {"SleepUntilIdle", ScriptFunction::SleepUntilIdle, nullptr, "o"},
    {"Stand", ScriptFunction::Stand, nullptr, "o"},
    {"StartAnimation", ScriptFunction::StartAnimation, nullptr, "os"},
    {"ShowCredits", ScriptFunction::ShowCredits, nullptr, "s"},
    {"ShowBitmap", ScriptFunction::ShowBitmap, nullptr, "fs"},
    {"SwitchLeverState", ScriptFunction::SwitchLeverState, nullptr, "of"},
    {"SwitchLeverStateEx", ScriptFunction::SwitchLeverStateEx, nullptr, "off"},
    {"UMAg", ScriptFunction::UMAg, nullptr, "of"},
    {"UMAggression", ScriptFunction::UMAggression, nullptr, "o"},
    {"UMClear", ScriptFunction::UMClear, nullptr, "o"},
    {"UMCorpseWatcher", ScriptFunction::UMCorpseWatcher, nullptr, "o"},
    {"UMFear", ScriptFunction::UMFear, nullptr, "of"},
    {"UMFollow", ScriptFunction::UMFollow, nullptr, "oo"},
    {"UMGuard", ScriptFunction::UMGuard, nullptr, "offf"},
    {"UMGuardEx", ScriptFunction::UMGuardEx, nullptr, "offff"},
    {"UMPatrol", ScriptFunction::UMPatrol, nullptr, "o"},
    {"UMPatrolAddPoint", ScriptFunction::UMPatrolAddPoint, nullptr, "off"},
    {"UMPatrolAddPointLook", ScriptFunction::UMPatrolAddPointLook, nullptr, "offf"},
    {"UMPatrolClear", ScriptFunction::UMPatrolClear, nullptr, "o"},
    {"UMPlayer", ScriptFunction::UMPlayer, nullptr, "o"},
    {"UMRevenge", ScriptFunction::UMRevenge, nullptr, "o"},
    {"UMSentry", ScriptFunction::UMSentry, nullptr, "off"},
    {"UMStandard", ScriptFunction::UMStandard, nullptr, "o"},
    {"UMSuspection", ScriptFunction::UMSuspection, nullptr, "o"},
    {"WaitEndAnimation", ScriptFunction::WaitEndAnimation, nullptr, ""},
    {"WaitSegment", ScriptFunction::WaitSegment, nullptr, "f"},
    {"Walk", ScriptFunction::Walk, nullptr, "o"},
    {"For", ScriptFunction::For, nullptr, "Og"},
    {"ForIf", ScriptFunction::ForIf, nullptr, "Ogf"},
    {"Every", ScriptFunction::Every, "FOgf", nullptr},
    {"Any", ScriptFunction::Any, "FOgf", nullptr},
    {"Sum", ScriptFunction::Sum, "FOgf", nullptr},
    {"IsEqual", ScriptFunction::IsEqual, "Fff", nullptr},
    {"IsLess", ScriptFunction::IsLess, "Fff", nullptr},
    {"IsGreater", ScriptFunction::IsGreater, "Fff", nullptr},
    {"IsEqualString", ScriptFunction::IsEqualString, "Fss", nullptr},
    {"Add", ScriptFunction::Add, "Fff", nullptr},
    {"Sub", ScriptFunction::Sub, "Fff", nullptr},
    {"Mul", ScriptFunction::Mul, "Fff", nullptr},
    {"Div", ScriptFunction::Div, "Fff", nullptr},
    {"Not", ScriptFunction::Not, "Ff", nullptr},
    {"HaveItem", ScriptFunction::HaveItem, "Fff", nullptr},
    {"GetObject", ScriptFunction::ScriptGetObject, "Of", nullptr},
    {"GetObjectByName", ScriptFunction::GetObjectByName, "Os", nullptr},
    {"GetObjectByID", ScriptFunction::GetObjectByID, "Os", nullptr},
    {"IsInSquare", ScriptFunction::IsInSquare, "Fffffff", nullptr},
    {"UnitInSquare", ScriptFunction::UnitInSquare, "Foffff", nullptr},
    {"GetMercsNumber", ScriptFunction::GetMercsNumber, "Ff", nullptr},
    {"GetZValue", ScriptFunction::GetZValue, "Fff", nullptr},
    {"GetBSZValue", ScriptFunction::GetBSZValue, "Fff", nullptr},
    {"HP", ScriptFunction::HP, "Fo", nullptr},
    {"MaxHP", ScriptFunction::MaxHP, "Fo", nullptr},
    {"Mana", ScriptFunction::Mana, "Fo", nullptr},
    {"MaxMana", ScriptFunction::MaxMana, "Fo", nullptr},
    {"DistanceUnitUnit", ScriptFunction::DistanceUnitUnit, "Foo", nullptr},
    {"DistanceUnitPoint", ScriptFunction::DistanceUnitPoint, "Foff", nullptr},
    {"Random", ScriptFunction::Random, "Ff", nullptr},
    {"GetLeverState", ScriptFunction::GetLeverState, "Fo", nullptr},
    {"GetX", ScriptFunction::GetX, "Fo", nullptr},
    {"GetY", ScriptFunction::GetY, "Fo", nullptr},
    {"GetZ", ScriptFunction::GetZ, "Fo", nullptr},
    {"GetFutureX", ScriptFunction::GetFutureX, "Fof", nullptr},
    {"GetFutureY", ScriptFunction::GetFutureY, "Fof", nullptr},
    {"GroupSize", ScriptFunction::GroupSize, "Fg", nullptr},
    {"GroupAdd", ScriptFunction::GroupAdd, "Ggg", nullptr},
    {"GroupSub", ScriptFunction::GroupSub, "Ggg", nullptr},
    {"GroupCross", ScriptFunction::GroupCross, "Ggg", nullptr},
    {"GroupHas", ScriptFunction::GroupHas, "Fgo", nullptr},
    {"GSGetVar", ScriptFunction::GSGetVar, "Ffs", nullptr},
    {"IsUnitInWater", ScriptFunction::IsUnitInWater, "Fo", nullptr},
    {"UnitSee", ScriptFunction::UnitSee, "Go", nullptr},
    {"GroupSee", ScriptFunction::GroupSee, "Gg", nullptr},
    {"PlayerSee", ScriptFunction::PlayerSee, "Gf", nullptr},
    {"GetDiplomacy", ScriptFunction::GetDiplomacy, "Fff", nullptr},
    {"IsEnemy", ScriptFunction::IsEnemy, "Fof", nullptr},
    {"IsAlive", ScriptFunction::IsAlive, "Fo", nullptr},
    {"IsCameraPlaying", ScriptFunction::IsCameraPlaying, "F", nullptr},
    {"IsDead", ScriptFunction::IsDead, "Fo", nullptr},
    {"IsPlayerInDanger", ScriptFunction::IsPlayerInDanger, "Ff", nullptr},
    {"IsPlayerInSafety", ScriptFunction::IsPlayerInSafety, "Ff", nullptr},
    {"GetPlayerUnits", ScriptFunction::GetPlayerUnits, "Gf", nullptr},
    {"GetUnitOfPlayer", ScriptFunction::GetUnitOfPlayer, "Off", nullptr},
    {"GetPlayer", ScriptFunction::GetPlayer, "Fo", nullptr},
    {"GetObjectID", ScriptFunction::GetObjectID, "Fo", nullptr},
    {"GetAIClass", ScriptFunction::GetAIClass, "Fo", nullptr},
    {"GetLootItemsCount", ScriptFunction::GetLootItemsCount, "Ff", nullptr},
    {"GetMoney", ScriptFunction::GetMoney, "Ff", nullptr},
    {"IsAlarm", ScriptFunction::IsAlarm, "Ff", nullptr},
    {"AlarmTime", ScriptFunction::AlarmTime, "Ff", nullptr},
    {"AlarmPosX", ScriptFunction::AlarmPosX, "Ff", nullptr},
    {"AlarmPosY", ScriptFunction::AlarmPosY, "Ff", nullptr},
    {"IsNight", ScriptFunction::IsNight, "F", nullptr},
    {"GetWorldTime", ScriptFunction::GetWorldTime, "F", nullptr},
    {"IsInArea", ScriptFunction::IsInArea, "Ffff", nullptr},
    {"WasLooted", ScriptFunction::WasLooted, "Fo", nullptr},
    {"IsUnitVisible", ScriptFunction::IsUnitVisible, "Fo", nullptr},
    {"GetLeader", ScriptFunction::GetLeader, "O", nullptr},
    {"IsUnitBlocked", ScriptFunction::IsUnitBlocked, "Fo", nullptr},
    {nullptr, ScriptFunction{}, nullptr, nullptr}
};

const ScriptKeyword* KeywordTable() {
    // Rebirth's AddNewScriptCommands points the classifier at its extended table. With the flag on,
    // the extended table is ours (script_addon.cpp, built from this one); else this one.
    if (scriptaddon::Enabled()) return scriptaddon::Table();
    return kScriptKeywords;
}

void ClassifyToken(const char* text, int* id, int* rest) {
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
        if (id) *id = static_cast<int>(k->id);
    }
}

const char* FunctionSignature(int id, int type) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        if (static_cast<int>(k->id) != id || !k->function) continue;
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

const char* KeywordName(int id) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
        if (static_cast<int>(k->id) == id) return k->name;
    return "Unknown command";
}

bool CScript::Tokenize(const std::string& source, std::list<ScriptToken>* tokens) {
    const char* p = source.c_str();

    tokens->clear();

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

        tokens->push_back({text, line});
    }
    return true;
}

bool CScript::ExpectLexeme(const std::list<ScriptToken>* tokens, std::list<ScriptToken>::const_iterator* pos, int id) {
    if (*pos == tokens->end()) return Error(this, tokens->back().line, 1, "Unexpected end of file");
    const ScriptToken token = *(*pos)++;
    std::string text = token.text;
    int found;
    ClassifyToken(text, &found, nullptr);
    if (found != id) return Error(this, token.line, 2, "Wrong lexeme %s", text);
    return true;
}

std::string* CScript::QualifyName(std::string* out, const std::string& name) {
    if (fileCount_ == 0) {
        *out = name;
        return out;
    }
    std::string qualified;
    qualified = str::Format("!%i!%s", fileCount_, name);
    *out = qualified;
    return out;
}

int CScript::FindScript(std::string_view name) {
    for (;;) {
        const std::vector<ScriptDefinition>& scripts = definitions_;
        for (std::size_t i = 0; i < scripts.size(); ++i)
            if (Mbscmp(scripts[i].name, name) == 0) return static_cast<int>(i);
        // Not found: retry without the leading "!<file>!" qualification.
        if (name.empty() || name[0] != '!') return -1;
        const std::size_t close = name.find('!', 1);
        if (close == std::string_view::npos) return -1;
        name.remove_prefix(close + 1);
    }
}

// ---- #define ------------------------------------------------------------------------------------

void AddDefine(std::list<ScriptDefine>* defines, const ScriptDefine* define) {
    if (defines->empty()) {
        defines->push_back(*define);
        return;
    }
    // Before the first define with a shorter name; when there is none, before the last one.
    auto before = defines->begin();
    while (before->name.size() >= define->name.size() && std::next(before) != defines->end()) ++before;
    defines->insert(before, *define);
}

void ApplyDefines(std::string* line, const std::list<ScriptDefine>* defines) {
    for (const ScriptDefine& define : *defines) {
        // Replaces from the start again after each replacement (a value containing its own name
        // never ends; kept).
        for (;;) {
            const std::string& text = *line;
            int at = str::Find(text, define.name);
            if (at < 0) break;
            std::string right = str::Right(text, static_cast<int>(line->size()) - static_cast<int>(define.name.size()) - at);
            std::string left = str::Left(text, at);
            *line = (left + define.value) + right;
        }
    }
}
