// Rebirth's extra script commands and functions (addon.dll's AddNewScriptCommands, v0.12.4.2): the extended keyword
// table, the dispatch to the handlers (script_addon_*.cpp) and the parser's '?' / '+' signature letters. See
// script_addon.h and docs/notes/rebirth.md.
// The table is the game's keywords (script.h's kScriptKeywords) plus the 69 records (name, signature, handler) and
// two of ours after them (a data effect's inputs): a name already in the table (case-insensitive) keeps its id and
// gets the record's signature as its function signature (the first letter is one of "FGOS") or command signature
// (both cleared first); a new name is appended with id 300 + the record's index. A map by id finds the record before the game's own switch runs.
#include "game/script/script_addon.h"
#include "game/script/script_addon_handlers.h"

#include "engine/core/engine_object.h"
#include "game/combat/combat.h"
#include "game/units/unit.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace scriptaddon {

namespace {

// A command record: the first letter alone makes a signature a function's.
struct Record {
    const char* name = nullptr;
    const char* signature = nullptr;
    Handler handler = nullptr;
};

// In addon's order (ids follow it).
const Record kRecords[] = {
    {"GetPlayerOfUnit", "Fo", GetPlayerOfUnit},
    {"GetUnitExp", "Fo?s", GetUnitExp},
    {"GetArmor", "Foff", GetArmor},
    {"GetObjectStringID", "So", GetObjectStringID},
    {"FloatToStr", "Sf?f", FloatToStr},
    {"StrToFloat", "Fs", StrToFloat},
    {"StrCat", "Sss+", StrCat},
    {"StrLen", "Fs", StrLen},
    {"Sin", "Ff", Sin},
    {"Cos", "Ff", Cos},
    {"Pow", "Fff", Pow},
    {"DistancePointPoint", "Fff", DistancePointPoint},   // sic: the handler reads four floats
    {"NearestUnit", "Offff", NearestUnit},
    {"UnitsNearPoint", "Gffff", UnitsNearPoint},
    {"GSSetPlayerVar", "fsf", GSSetPlayerVar},
    {"GSGetPlayerVar", "Ffs", GSGetPlayerVar},
    {"GiveExp", "ff", GiveExp},
    {"GiveUnitExp", "of", GiveUnitExp},
    {"StartCameraSave", "s", StartCameraSave},
    {"StopCameraSave", "", StopCameraSave},
    {"Say", "s+", Say},
    {"ConsoleString", "s+", ConsoleString},
    {"Add", "Fff+", Add},
    {"Mul", "Fff+", Mul},
    {"And", "Fff+", And},
    {"Or", "Fff+", Or},
    {"Max", "Fff+", Max},
    {"Min", "Fff+", Min},
    {"Abs", "Ff", Abs},
    {"Round", "Ff", Round},
    {"Ceil", "Ff", Ceil},
    {"Floor", "Ff", Floor},
    {"Random", "Ff?f", Random},
    {"RandStr", "Sss+", RandStr},
    {"RandObj", "Og", RandObj},
    {"StrHas", "Fss", StrHas},
    {"StrMid", "Ssff", StrMid},
    {"StrLower", "Ss", StrLower},
    {"StrUpper", "Ss", StrUpper},
    {"ResetTarget", "o", ResetTarget},
    {"SendCurrentParty", "f", SendCurrentParty},
    {"PlayerExists", "Ff", PlayerExists},
    {"Loot", "fo", Loot},
    {"GameOver", "", GameOver},
    {"GameOverMSG", "ss", GameOverMsg},
    {"GetDexterity", "Fo", GetDexterity},
    {"GetIntelligence", "Fo", GetIntelligence},
    {"GetStrength", "Fo", GetStrength},
    {"GetSkill", "Fos", GetSkill},
    {"GetActions", "Fo", GetActions},
    {"GetActionsBase", "Fo", GetActionsBase},
    {"GetEncumbrance", "Fo", GetEncumbrance},
    {"GetMaxEncumbrance", "Fo", GetMaxEncumbrance},
    {"GetPerk", "Fos", GetPerk},
    {"GetPrototype", "So", GetPrototype},
    {"GetRace", "So", GetRace},
    {"IsFileExists", "Fs", IsFileExists},
    {"GSListPlayerVars", "f", GSListPlayerVars},
    {"GetObjectTypeID", "Fo", GetObjectTypeID},
    {"GetObjectsOnMap", "Gf+", GetObjectsOnMap},
    {"IsPlayer", "Fo", IsPlayer},
    {"GiveUnitWeapon", "osf", GiveUnitWeapon},
    {"GiveUnitArmor", "osf", GiveUnitArmor},
    {"IsDead", "Fo", IsDead},
    {"IsAlive", "Fo", IsAlive},
    {"ShowAllUnits", "f", ShowAllUnits},
    {"PauseGame", "f", PauseGame},
    {"SetWeather", "s", SetWeather},
    {"AddMobEx", "s?f", AddMobEx},
    // Ours, after addon's: a data effect's inputs set on the server (script_effects.cpp).
    {"SetParticleInput", "fsf?ff", SetParticleInput},
    {"FireParticleEvent", "fs", FireParticleEvent},
};
constexpr int kRecordCount = static_cast<int>(sizeof(kRecords) / sizeof(kRecords[0]));
static_assert(kRecordCount == 69 + 2);
constexpr int kFirstNewId = 300;

bool IsAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsAsciiAlnum(char c) { return IsAsciiAlpha(c) || (c >= '0' && c <= '9'); }

bool IsVanillaKept(const char* name) {
    for (const char* kept : {"IsAlive", "IsDead", "ResetTarget"})
        if (Stricmp(name, kept) == 0) return true;
    return false;
}

struct Built {
    std::vector<ScriptKeyword> table;
    signed char recordById[kFirstNewId + kRecordCount] = {};  // id -> record index, -1 for none
};

const Built& Build() {
    static const Built built = [] {
        Built b;
        std::memset(b.recordById, -1, sizeof(b.recordById));
        b.table.reserve(300);
        for (const ScriptKeyword* k = kScriptKeywords; k->name; ++k) b.table.push_back(*k);
        for (int r = 0; r < kRecordCount; ++r) {
            const Record& record = kRecords[r];
            bool function = std::strchr("FGOS", record.signature[0]) != nullptr && record.signature[0] != 0;
            ScriptKeyword* existing = nullptr;
            for (ScriptKeyword& k : b.table)
                if (Stricmp(k.name, record.name) == 0) {
                    existing = &k;
                    break;
                }
            // A command the game has keeps the game's own where addon's would change what the game's scripts do
            // (IsAlive / IsDead: units only and another test; ResetTarget: another reset).
            if (existing && IsVanillaKept(record.name)) continue;
            if (existing) {
                existing->function = nullptr;
                existing->command = nullptr;
                (function ? existing->function : existing->command) = record.signature;
                b.recordById[static_cast<int>(existing->id)] = static_cast<signed char>(r);
            } else {
                int id = kFirstNewId + r;
                b.table.push_back({record.name, static_cast<ScriptFunction>(id), function ? record.signature : nullptr,
                                   function ? nullptr : record.signature});
                b.recordById[id] = static_cast<signed char>(r);
            }
        }
        b.table.push_back({nullptr, ScriptFunction{}, nullptr, nullptr});
        return b;
    }();
    return built;
}


} // namespace

// ---- Helpers for the handlers -------------------------------------------------------------------

int TruncateOrMin(float value) {
    const double wide = value;
    if (!(wide > -2147483649.0 && wide < 2147483648.0)) return INT32_MIN;
    return static_cast<int>(wide);
}

float Distance(float x0, float y0, float x1, float y1) { return Length2D(Vector2{x1, y1} - Vector2{x0, y0}); }

int Stricmp(std::string_view a, std::string_view b) {
    for (std::size_t i = 0;; ++i) {
        int ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0, cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb || ca == 0) return ca - cb;
    }
}

bool IsUnit(EngineObject* object) { return object->GetClassId() == ClassId::Unit; }

bool Report(const char*, const char*, ...) { return true; }

bool InvalidArguments(const char* function) {
    return Report(function, "Invalid argument(s) passed to function");
}



const ScriptKeyword* Table() { return Build().table.data(); }

bool Dispatch(Call* call) {
    const int id = static_cast<int>(call->cmd->id);
    if (id < 0 || id >= kFirstNewId + kRecordCount) return false;
    int r = Build().recordById[id];
    if (r < 0 || !kRecords[r].handler) return false;
    return kRecords[r].handler(call);
}

bool RejectsMatch(const char* keyword, char next) {
    return IsAsciiAlpha(keyword[0]) && (IsAsciiAlnum(next) || next == '_' || next == '#');
}

void SignatureStep(std::string_view signature, std::size_t* letter, int tokenId) {
    std::size_t& p = *letter;
    // The letter at p (a zero byte past the end, as the C string it was).
    const auto at = [&] { return p < signature.size() ? signature[p] : '\0'; };
    const bool repeat = at() == '+';
    const bool optional = at() == '?';
    if (!repeat && !optional) return;
    if (tokenId == ScriptLexeme::Close) {
        if (repeat) {
            ++p;
            return;
        }
        while (at()) ++p;   // the optional parameters are left out
        return;
    }
    if (optional) {
        ++p;
        return;
    }
    --p;   // back to the repeated type
}

void CalcString(ScriptCall* cmd, Script* script, std::string* result) {
    Call call{cmd, script, result};
    if (Dispatch(&call)) return;
    game::FatalExit("Function does not return string at ScriptCall::CalcFunction");
}

} // namespace scriptaddon
