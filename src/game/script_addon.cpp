// Rebirth's AddNewScriptCommands (addon.dll v0.12.4.2): the extended keyword table, the dispatch of
// addon's script functions and commands, and the parser changes. See script_addon.h and
// docs/notes/rebirth.md. The handlers are in script_addon_*.cpp.
// How addon installs it: a std::vector of 300 entries gets the game's keywords (script.h's
// kScriptKeywords, without the terminator); then each of its 69 command records (0x40 bytes: name,
// signature, handler, flags) is registered: a name already in the table (stricmp) with
// an id below 300 keeps its id and gets the record's signature as its function signature (the
// first letter is one of "FGOS") or command signature (both cleared first); a new name is appended
// with id 300 + the number of records registered before it. The terminator is appended, the vector
// shrunk to fit, and the loader's (and the dumper's) references to the keyword table are
// pointed at it. The records also go into a hash map by id that the hooks look the command up in
// before the game's own switch.
#include "script_addon.h"
#include "script_addon_handlers.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "engine_object.h"
#include "combat.h"
#include "unit.h"
#include "lib/crt.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace scriptaddon {

namespace {

// A command record (100BA738): flags are 0 for every record, so the first letter alone makes a
// signature a function's.
struct Record {
    const char* name;
    const char* signature;
    Handler handler;
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
};
constexpr int kRecordCount = static_cast<int>(sizeof(kRecords) / sizeof(kRecords[0]));
static_assert(kRecordCount == 69);
constexpr int kFirstNewId = 300;

// _stricmp in the "C" locale (the names are ASCII).
int AsciiStricmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        int ca = static_cast<unsigned char>(*a), cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb || ca == 0) return ca - cb;
    }
}

bool IsAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsAsciiAlnum(char c) { return IsAsciiAlpha(c) || (c >= '0' && c <= '9'); }

struct Built {
    std::vector<ScriptKeyword> table;
    signed char recordById[kFirstNewId + kRecordCount];   // id -> record index, -1 for none
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
                if (AsciiStricmp(k.name, record.name) == 0) {
                    existing = &k;
                    break;
                }
            if (existing) {
                existing->function = nullptr;
                existing->command = nullptr;
                (function ? existing->function : existing->command) = record.signature;
                b.recordById[existing->id] = static_cast<signed char>(r);
            } else {
                int id = kFirstNewId + r;
                b.table.push_back({record.name, id, function ? record.signature : nullptr,
                                   function ? nullptr : record.signature});
                b.recordById[id] = static_cast<signed char>(r);
            }
        }
        b.table.push_back({nullptr, 0, nullptr, nullptr});
        return b;
    }();
    return built;
}


} // namespace

// ---- Helpers for the handlers -------------------------------------------------------------------

// CVTTSD2SI (addon.dll's conversion): truncation; NaN and values outside the int range give INT_MIN.
int Ftol(float value) {
    const double wide = value;
    if (!(wide > -2147483649.0 && wide < 2147483648.0)) return INT32_MIN;
    return static_cast<int>(wide);
}

void Store(float* dst, float value) { x87::CopyBits(*dst, value); }

// The game's _ftol: truncation of the 64-bit value, its low 32 bits.
int TruncateX87(float value) { return static_cast<int>(libcrt::Ftol(value)); }

float Distance(float x0, float y0, float x1, float y1) {
    const float deltaX = x1 - x0;
    const float deltaY = y1 - y0;
    const float squared = static_cast<float>(LengthSquared2DUnrounded<double>(deltaY, deltaX));
    return static_cast<float>(std::sqrt(squared));
}

int Stricmp(const char* a, const char* b) { return AsciiStricmp(a, b); }

bool IsUnit(CEngineObject* object) { return object->GetClassId() == ClassId::Unit; }

CUnitServer* UnitData(const ScriptObjectRef* ref) {
    if (!ref) return nullptr;
    CUnit* unit = UnitOrNull(ref->object);
    return unit ? unit->ServerController() : nullptr;
}

bool Report(const char*, const char*, ...) { return true; }

bool InvalidArguments(const char* function) {
    return Report(function, "Invalid argument(s) passed to function");
}


bool Enabled() { return ei::rebirth::Enabled(ei::rebirth::Patch::AddNewScriptCommands); }

const ScriptKeyword* Table() { return Build().table.data(); }

bool Dispatch(Call* call) {
    int id = call->cmd->id;
    if (id < 0 || id >= kFirstNewId + kRecordCount) return false;
    int r = Build().recordById[id];
    if (r < 0 || !kRecords[r].handler) return false;
    return kRecords[r].handler(call);
}

bool RejectsMatch(const char* keyword, char next) {
    return IsAsciiAlpha(keyword[0]) && (IsAsciiAlnum(next) || next == '_' || next == '#');
}

void SignatureStep(SSCCmd* cmd, const char** signature, int tokenId) {
    const char*& p = *signature;
    bool repeat = *p == '+';
    bool optional = *p == '?';
    if (!repeat && !optional) return;
    if (tokenId == kLexClose) {
        if (repeat) {
            ++p;
            return;
        }
        int floats = 0, strings = 0, objects = 0, groups = 0;
        if (*p == 0) return;
        do {
            switch (*p) {
            case 'f': ++floats; break;
            case 'g': ++groups; break;
            case 'o': ++objects; break;
            case 's': ++strings; break;
            }
            ++p;
        } while (*p);
        cmd->floatArgs.resize(cmd->floatArgs.size() - floats);
        cmd->stringArgs.resize(cmd->stringArgs.size() - strings);
        cmd->objectArgs.resize(cmd->objectArgs.size() - objects);
        cmd->groupArgs.resize(cmd->groupArgs.size() - groups);
        return;
    }
    if (optional) {
        ++p;
        return;
    }
    --p;
    switch (*p) {
    case 's':
        cmd->stringArgs.emplace_back();
        break;
    case 'o':
        cmd->objectArgs.push_back(nullptr);
        break;
    case 'g':
        cmd->groupArgs.push_back(nullptr);
        break;
    case 'f':
        cmd->floatArgs.push_back(0.0f);
        break;
    }
}

void CalcString(SSCCmd* cmd, CScript* script, ScriptVars* vars, std::string* result) {
    cmd->PrepareArguments(script, vars);
    Call call{cmd, script, result, nullptr, nullptr};
    if (Dispatch(&call)) return;
    game::FatalError("Function does not return string at SSCCmd::CalcFunction");
}

} // namespace scriptaddon
