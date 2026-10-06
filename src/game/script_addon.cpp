// Rebirth's AddNewScriptCommands (addon.dll v0.12.4.2): the extended keyword table, the dispatch of
// addon's script functions and commands, and the parser changes. See script_addon.h and
// docs/notes/rebirth.md. The handlers are in script_addon_*.cpp.
//
// How addon installs it (10022290): a std::vector of 300 entries gets the game's keywords (0078BCA0,
// without the terminator); then each of its 69 command records (100BA738, 0x40 bytes: name,
// signature, handler, flags) is registered (10021C61): a name already in the table (stricmp) with
// an id below 300 keeps its id and gets the record's signature as its function signature (the
// first letter is one of "FGOS") or command signature (both cleared first); a new name is appended
// with id 300 + the number of records registered before it. The terminator is appended, the vector
// shrunk to fit, and the 15 references to 0078BCA0 in the loader (and the dumper 004D41C0) are
// pointed at it. The records also go into a hash map by id that the hooks look the command up in
// (10021D96) before the game's own switch.
#include "script_addon.h"
#include "script_addon_handlers.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "engine_object.h"
#include "lib/crt.h"

#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

#pragma warning(disable : 4740) // inline assembly

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

const ScriptKeyword* GameTable() { return reinterpret_cast<const ScriptKeyword*>(0x0078BCA0); }

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
        for (const ScriptKeyword* k = GameTable(); k->name; ++k) b.table.push_back(*k);
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

// addon's CArray SetSize(n) for the argument arrays (10022686 floats, 1002259F strings, 10022755
// object and group pointers): MFC's, through the game's heap, except that a full array grows to
// size + growBy elements, growBy 0 meaning 4 (its clamp of size / 8 always gives 4).
template <typename T>
void ArgumentsSetSize(mfc::CArray<T>& a, int n) {
    static_assert(sizeof(T) == 4);
    constexpr bool strings = std::is_same_v<T, mfc::CString>;
    if (n <= a.size) {
        if constexpr (strings)
            for (int i = n; i < a.size; ++i) a.data[i].~CString();
        if (n == 0) {
            gamecrt::operator_delete(a.data);
            a.data = nullptr;
            a.maxSize = 0;
        }
        a.size = n;
        return;
    }
    if (a.maxSize < n) {
        int grow = a.growBy != 0 ? a.growBy : 4;
        int newMax = a.size + grow;
        if (newMax < n) newMax = n;
        T* data = static_cast<T*>(gamecrt::operator_new(static_cast<unsigned>(newMax) * 4));
        std::memcpy(data, a.data, static_cast<unsigned>(a.size) * 4);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.maxSize = newMax;
    }
    std::memset(a.data + a.size, 0, static_cast<unsigned>(n - a.size) * 4);
    if constexpr (strings)
        for (int i = a.size; i < n; ++i) new (&a.data[i]) mfc::CString();
    a.size = n;
}

} // namespace

// ---- Helpers for the handlers -------------------------------------------------------------------

int Ftol(float value) {
    double wide = value;
    int result;
    __asm {
        cvttsd2si eax, qword ptr [wide]
        mov result, eax
    }
    return result;
}

int Status(float a, float b) {
    int status;
    __asm {
        fld a
        fcomp b
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return status;
}

void Store(float* dst, float value) {
    __asm {
        fld value
        mov eax, dst
        fstp dword ptr [eax]
    }
}

int TruncateX87(float value) {
    int result;
    __asm {
        fld value
        mov eax, 0x006E853C   // the game's _ftol: truncating FISTP of the 64-bit value
        call eax
        mov result, eax
    }
    return result;
}

float Distance(float x0, float y0, float x1, float y1) {
    float deltaX, deltaY, squared;
    __asm {
        fld x1
        fsub x0
        fstp deltaX
        fld y1
        fsub y0
        fstp deltaY
        fld deltaY
        fld deltaX
        fld st(0)
        fmulp st(1), st
        fld st(1)
        fmulp st(2), st
        faddp st(1), st
        fstp squared
    }
    return static_cast<float>(x87::fsqrt(squared));
}

int Stricmp(const char* a, const char* b) { return AsciiStricmp(a, b); }

bool IsUnit(CEngineObject* object) { return object->GetClassId() == 0x50; }

void* UnitData(const ScriptObjectRef* ref) {
    if (!ref || !ref->object) return nullptr;
    if (!IsUnit(ref->object)) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(ref->object) + 0x240);
}

bool Report(const char*, const char*, ...) { return true; }

bool InvalidArguments(const char* function) {
    return Report(function, "Invalid argument(s) passed to function");
}

// ---- Interface ----------------------------------------------------------------------------------

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
        if (floats) ArgumentsSetSize(cmd->floatArgs, cmd->floatArgs.size - floats);
        if (strings) ArgumentsSetSize(cmd->stringArgs, cmd->stringArgs.size - strings);
        if (objects) ArgumentsSetSize(cmd->objectArgs, cmd->objectArgs.size - objects);
        if (groups) ArgumentsSetSize(cmd->groupArgs, cmd->groupArgs.size - groups);
        return;
    }
    if (optional) {
        ++p;
        return;
    }
    --p;
    switch (*p) {
    case 's': {
        mfc::CString empty;
        empty = "";
        int n = cmd->stringArgs.size;
        ArgumentsSetSize(cmd->stringArgs, n + 1);
        cmd->stringArgs.data[n] = static_cast<const char*>(empty);
        break;
    }
    case 'o': {
        int n = cmd->objectArgs.size;
        ArgumentsSetSize(cmd->objectArgs, n + 1);
        cmd->objectArgs.data[n] = nullptr;
        break;
    }
    case 'g': {
        int n = cmd->groupArgs.size;
        ArgumentsSetSize(cmd->groupArgs, n + 1);
        cmd->groupArgs.data[n] = nullptr;
        break;
    }
    case 'f': {
        int n = cmd->floatArgs.size;
        ArgumentsSetSize(cmd->floatArgs, n + 1);
        cmd->floatArgs.data[n] = 0.0f;
        break;
    }
    }
}

void CalcString(SSCCmd* cmd, CScript* script, ScriptVars* vars, mfc::CString* result) {
    cmd->PrepareArguments(script, vars);
    Call call{cmd, script, result, nullptr, nullptr};
    if (Dispatch(&call)) return;
    game::FatalError(reinterpret_cast<const char*>(0x0078B748));   // "Function does not return string at ..."
}

} // namespace scriptaddon
