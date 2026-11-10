// Script loader, part 1: tokens, the lexer (Script::Tokenize), keyword lookup, #define handling
// and name helpers. The parser proper is in script_parse_load.cpp. See script.h and
// docs/notes/script.md ("Loader and parser").
#include "game/script/script.h"
#include "game/script/script_parse.h"
#include "game/script/script_addon.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <format>

using namespace scriptparse;



int scriptparse::ScriptErrorV(Script* script, int line, ScriptParseError code, const char* format, va_list args) {
    char text[0x400];   // cut to 1023 bytes (the game's wvsprintf wrote at most 1024 characters)
    std::vsnprintf(text, sizeof(text), format, args);
    script->errorLine = line;
    script->errorText = text;
    script->errorCode = code;
    return 0;
}

// ---- Keyword table ------------------------------------------------------------------------------

constexpr ScriptFunction Lex(ScriptLexeme lexeme) { return static_cast<ScriptFunction>(lexeme); }

// The game's table (script.h): the punctuation and section keywords, the commands, the functions.
const ScriptKeyword kScriptKeywords[226] = /*0078BCA0*/ {
    {"(", Lex(ScriptLexeme::Open), nullptr, nullptr},
    {")", Lex(ScriptLexeme::Close), nullptr, nullptr},
    {",", Lex(ScriptLexeme::Comma), nullptr, nullptr},
    {":", Lex(ScriptLexeme::Colon), nullptr, nullptr},
    {"=", Lex(ScriptLexeme::Assign), nullptr, nullptr},
    {"WorldScript", Lex(ScriptLexeme::WorldScript), nullptr, nullptr},
    {"Script", Lex(ScriptLexeme::Script), nullptr, nullptr},
    {"DeclareScript", Lex(ScriptLexeme::DeclareScript), nullptr, nullptr},
    {"GlobalVars", Lex(ScriptLexeme::GlobalVars), nullptr, nullptr},
    {"if", Lex(ScriptLexeme::If), nullptr, nullptr},
    {"then", Lex(ScriptLexeme::Then), nullptr, nullptr},
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
    // The game's table extended by script_addon.cpp.
    return scriptaddon::Table();
}

void ClassifyToken(std::string_view token, int* id, int* rest) {
    token = token.substr(0, token.find('\0'));   // the text of a C string: up to its first zero byte
    std::size_t p = 0;
    const auto at = [&](std::size_t i) { return i < token.size() ? token[i] : '\0'; };
    if (text::IsDigit(at(p)) || at(p) == '.' || at(p) == '-') {
        // Number: digits and at most one '.', after an optional leading '-' or '.'.
        int dots = at(p) == '.';
        ++p;
        while (p < token.size()) {
            if (!text::IsDigit(token[p]) && token[p] != '.') break;
            if (token[p] == '.' && dots++ != 0) break;
            ++p;
        }
        if (id) *id = static_cast<int>(ScriptLexeme::Number);
        if (rest) *rest = static_cast<int>(token.size() - p);
        return;
    }
    const char quote = kQuote;
    if (at(p) == quote) {
        // String: up to the closing quote; *rest counts what follows it.
        ++p;
        while (p < token.size() && token[p] != quote) ++p;
        if (p < token.size()) {
            if (id) *id = static_cast<int>(ScriptLexeme::String);
            if (rest) *rest = static_cast<int>(token.size() - p) - 1;
            return;
        }
        // Unterminated: the keyword search below runs on the end of the text (finds nothing).
    }
    if (rest) *rest = 0;
    if (id) *id = 0;
    const std::string_view remaining = token.substr(p);
    int best = -1;
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        const std::string_view name = k->name;
        int length = static_cast<int>(name.size());
        if (length < best) continue;
        if (text::CompareIgnoringCase(remaining.substr(0, name.size()), name) != 0) continue;
        best = length;
        // An identifier-like match must end the identifier.
        const char next = name.size() < remaining.size() ? remaining[name.size()] : '\0';
        if (scriptaddon::RejectsMatch(k->name, next)) continue;
        if (rest) *rest = static_cast<int>(remaining.size()) - length;
        if (id) *id = static_cast<int>(k->id);
    }
}

std::optional<std::string_view> FunctionSignature(int id, ScriptType type) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        if (static_cast<int>(k->id) != id || !k->function) continue;
        // The first letter is the result's type (F, G, O or S).
        if (type != ScriptType::None && ScriptTypeOfLetter(k->function[0]) == type) return std::string_view(k->function + 1);
    }
    return std::nullopt;
}

const char* KeywordName(ScriptLexeme id) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
        if (static_cast<int>(k->id) == static_cast<int>(id)) return k->name;
    return "Unknown command";
}

bool Script::Tokenize(const std::string& source, std::vector<ScriptToken>* tokens) {
    // The source up to its first zero byte, where the C string ended; `at` reads a zero byte past the end.
    const std::string_view chars = std::string_view(source).substr(0, source.find('\0'));
    const auto at = [&](std::size_t i) { return i < chars.size() ? chars[i] : '\0'; };
    std::size_t p = 0;

    tokens->clear();

    const char quote = kQuote;
    int line = 1;
    while (p < chars.size()) {
        while (text::IsSpace(at(p))) {
            if (at(p) == '\n') ++line;
            ++p;
            if (p >= chars.size()) break;
        }

        // A token runs to white space (a space inside quotes does not count) or to one of , ) ( : =
        // after its first character (not inside quotes). Keywords are split off afterwards.
        const std::size_t start = p;
        int inQuote = 0;
        while (p < chars.size()) {
            const char c = chars[p];
            if (text::IsSpace(c) && (c != ' ' || !inQuote)) break;
            if (c == quote) inQuote = !inQuote;
            if ((c == ',' || c == ')' || c == '(' || c == ':' || c == '=') && p != start && !inQuote) break;
            if (c == '\n') break;
            ++p;
        }
        const std::size_t length = std::min(p - start, std::size_t{0xFF});
        if (length == 0) continue;
        const std::string_view buffer = chars.substr(start, length);

        if (buffer.starts_with(kCommentOpen)) {
            // Block comment: the search for the end starts after the token (so "/**/" written as
            // one token does not end it) and skips quoted text.
            if (p >= chars.size()) return Error(this, line, ScriptParseError::EndOfFile, "End of file before '%s'", kCommentClose);
            for (;;) {
                const char c = chars[p];
                if (c == '\n') ++line;
                if (c == quote) inQuote = !inQuote;
                if (!inQuote && chars.substr(p).starts_with(kCommentClose)) {
                    p += std::string_view(kCommentClose).size();
                    break;
                }
                ++p;
                if (p >= chars.size()) return Error(this, line, ScriptParseError::EndOfFile, "End of file before '%s'", kCommentClose);
            }
            continue;
        }
        if (text::CompareIgnoringCase(buffer.substr(0, std::string_view(kLineComment).size()), kLineComment) == 0) {
            while (p < chars.size() && chars[p] != '\n') ++p;
            continue;
        }

        // Keep only the part ClassifyToken recognises (a keyword prefix, a number, a quoted
        // string); the rest is scanned again as the next token.
        int rest;
        ClassifyToken(buffer, nullptr, &rest);
        p -= static_cast<std::size_t>(rest);
        tokens->push_back({std::string(buffer.substr(0, buffer.size() - static_cast<std::size_t>(rest))), line});
    }
    return true;
}

ScriptToken TokenReader::Next(int* id) {
    if (AtEnd()) {
        if (id) *id = 0;
        return ScriptToken{std::string(), LastLine()};
    }
    ScriptToken token = *pos++;
    if (id) ClassifyToken(token.text, id, nullptr);
    return token;
}

bool TokenReader::Expect(ScriptLexeme lexeme) {
    if (AtEnd()) return EndOfFileError();
    int found;
    const ScriptToken token = Next(&found);
    if (found != lexeme) return Script::Error(script, token.line, ScriptParseError::WrongLexeme, "Wrong lexeme %s", token.text);
    return true;
}

bool TokenReader::EndOfFileError() { return Script::Error(script, LastLine(), ScriptParseError::EndOfFile, "Unexpected end of file"); }

std::string Script::QualifyName(const std::string& name) {
    if (fileCount == 0) return name;
    return std::format("!{}!{}", fileCount, name);
}

int Script::FindScript(std::string_view name) {
    for (;;) {
        const std::vector<ScriptDefinition>& scripts = definitions;
        for (std::size_t i = 0; i < scripts.size(); ++i)
            if (str::Compare(scripts[i].name, name) == 0) return static_cast<int>(i);
        // Not found: retry without the leading "!<file>!" qualification.
        if (name.empty() || name[0] != '!') return -1;
        const std::size_t close = name.find('!', 1);
        if (close == std::string_view::npos) return -1;
        name.remove_prefix(close + 1);
    }
}

// ---- #define ------------------------------------------------------------------------------------

void AddDefine(std::vector<ScriptDefine>* defines, const ScriptDefine* define) {
    if (defines->empty()) {
        defines->push_back(*define);
        return;
    }
    // Before the first define with a shorter name; when there is none, before the last one.
    auto before = defines->begin();
    while (before->name.size() >= define->name.size() && std::next(before) != defines->end()) ++before;
    defines->insert(before, *define);
}

void ApplyDefines(std::string* line, std::vector<ScriptDefine>* defines) {
    for (ScriptDefine& define : *defines) {
        // Replaces from the start again after each replacement (a value containing its own name
        // never ends; kept).
        for (;;) {
            const std::string& text = *line;
            int at = str::Find(text, define.name);
            if (at < 0) break;
            std::string right = str::Right(text, static_cast<int>(line->size()) - static_cast<int>(define.name.size()) - at);
            std::string left = str::Left(text, at);
            *line = (left + define.value) + right;
            ++define.uses;
        }
    }
}
