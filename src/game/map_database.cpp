// The map database loader (0045EA20). See map_database.h for the file format.
#include "map_database.h"

#include "console.h"
#include "object_tables.h"

#include "../lib/crt.h"
#include "ei/hook.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace mapdb {
namespace {

// The text manager (item_db_text.h) as the loader uses it. Not rewritten.
struct TextReader {
    mfc::BOOL Open(const char* name);                          // 004DEFC0: finds the text, cursor at its start
    mfc::BOOL NextLine(int flag);                              // 004DF030: false at the end
    int Split(console::CStringArray* words, int flag);         // 004DFCB0: the current line's words
    mfc::BOOL Close();                                         // 004DF020
};
__declspec(naked) mfc::BOOL TextReader::Open(const char*) EI_ORIGINAL(0x004DEFC0)
__declspec(naked) mfc::BOOL TextReader::NextLine(int) EI_ORIGINAL(0x004DF030)
__declspec(naked) int TextReader::Split(console::CStringArray*, int) EI_ORIGINAL(0x004DFCB0)
__declspec(naked) mfc::BOOL TextReader::Close() EI_ORIGINAL(0x004DF020)
TextReader* TheTextReader() { return *reinterpret_cast<TextReader**>(0x007AF788); }

// Element constructors / destructors of the record arrays (not rewritten).
using ElementFn = void(__stdcall*)(void* elements, int count);
__declspec(naked) void __stdcall ConstructAllods(void*, int) EI_ORIGINAL(0x00460720)
__declspec(naked) void __stdcall DestructAllods(void*, int) EI_ORIGINAL(0x004606F0)
__declspec(naked) void __stdcall ConstructMaps(void*, int) EI_ORIGINAL(0x004607E0)
__declspec(naked) void __stdcall DestructMaps(void*, int) EI_ORIGINAL(0x004607B0)
__declspec(naked) void __stdcall ConstructExits(void*, int) EI_ORIGINAL(0x004606A0)
__declspec(naked) void __stdcall DestructExits(void*, int) EI_ORIGINAL(0x00460670)
// CDWordArray::SetAtGrow (the allod's map list).
__declspec(naked) void __fastcall SetAtGrowDword(void* /*array*/, void*, int /*index*/, std::uint32_t /*value*/) EI_ORIGINAL(0x00460500)

__declspec(naked) int __cdecl atoi(const char*) EI_ORIGINAL(0x006E80D8)
__declspec(naked) double __cdecl atof(const char*) EI_ORIGINAL(0x006E911A)

// *out = float(atof(text) * (pi * 1/180)): degrees to radians on the FPU as the original does
// (the factor is the product of two doubles at 0073C3B8 and 0073BA30, rounded by the FPU).
__declspec(naked) void __cdecl StoreRadians(void* /*out*/, const char* /*text*/) {
    __asm {
        mov eax, dword ptr [esp + 8]
        push eax
        mov eax, 0x006E911A
        call eax
        add esp, 4
        mov eax, 0x0073C3B8
        fld qword ptr [eax]
        mov eax, 0x0073BA30
        fmul qword ptr [eax]
        fmulp st(1), st
        mov eax, dword ptr [esp + 4]
        fstp dword ptr [eax]
        ret
    }
}

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
bool Is(const char* word, std::uintptr_t text) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(word), reinterpret_cast<const unsigned char*>(GameText(text))) == 0;
}

constexpr std::uintptr_t kAllodText = 0x00785768;     // "#allod"
constexpr std::uintptr_t kZoneText = 0x00785760;      // "#zone"
constexpr std::uintptr_t kGameText = 0x00785758;      // "game"
constexpr std::uintptr_t kBriefText = 0x00785750;     // "brief"
constexpr std::uintptr_t kEdgeText = 0x00785748;      // "edge"
constexpr std::uintptr_t kResText = 0x00785740;       // "#res"
constexpr std::uintptr_t kCameraText = 0x00785738;    // "#camera"
constexpr std::uintptr_t kMapsText = 0x00785730;      // "#maps"
constexpr std::uintptr_t kFigureText = 0x00785728;    // "#figure"
constexpr std::uintptr_t kWeatherText = 0x0078571C;   // "#weather"
constexpr std::uintptr_t kNoneText = 0x007846F0;      // "none"
constexpr std::uintptr_t kSnowText = 0x00785714;      // "snow"
constexpr std::uintptr_t kTornadoText = 0x0078570C;   // "tornado"
constexpr std::uintptr_t kCageText = 0x00785704;      // "#cage"
constexpr std::uintptr_t kRestrictText = 0x007856F8;  // "#restrict"
constexpr std::uintptr_t kSkyText = 0x007856F0;       // "#sky"
constexpr std::uintptr_t kCaveText = 0x007856E8;      // "cave"
constexpr std::uintptr_t kExitText = 0x007856E0;      // "#exit"
constexpr std::uintptr_t kDeployText = 0x007856D8;    // "#deploy"
constexpr std::uintptr_t kRemoveText = 0x007856D0;    // "#remove"
constexpr std::uintptr_t kPassTimeText = 0x007856C4;  // "#passtime"
constexpr std::uintptr_t kViewText = 0x007856BC;      // "#view"
constexpr std::uintptr_t kZeroText = 0x007856B8;      // "0"
constexpr std::uintptr_t kDeployAngleText = 0x007856A8;   // "#deployangle"

// The body of an MFC CArray (after its vtable).
struct ArrayBody {
    char* data;
    int size;
    int max;
    int grow;
};

// CArray::SetSize(size, -1) as the original inlines it.
void SetSize(ArrayBody& a, int size, int elementSize, ElementFn construct, ElementFn destruct) {
    if (size == 0) {
        if (a.data) {
            destruct(a.data, a.size);
            gamecrt::operator_delete(a.data);
            a.data = nullptr;
        }
        a.max = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = static_cast<char*>(gamecrt::operator_new(size * elementSize));
        construct(a.data, size);
        a.max = size;
        a.size = size;
    } else if (size <= a.max) {
        if (size > a.size) construct(a.data + a.size * elementSize, size - a.size);
        else if (size < a.size) destruct(a.data + size * elementSize, a.size - size);
        a.size = size;
    } else {
        int grow = a.grow;
        if (grow == 0) {
            grow = a.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int capacity = a.max + grow;
        if (size >= capacity) capacity = size;
        char* data = static_cast<char*>(gamecrt::operator_new(capacity * elementSize));
        std::memcpy(data, a.data, a.size * elementSize);
        construct(data + a.size * elementSize, size - a.size);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.size = size;
        a.max = capacity;
    }
}

ArrayBody& Body(void* array) { return *reinterpret_cast<ArrayBody*>(static_cast<char*>(array) + 4); }
mfc::CString& StringAt(char* p, std::size_t offset) { return *reinterpret_cast<mfc::CString*>(p + offset); }
template <typename T>
void Set(char* p, std::size_t offset, T v) { std::memcpy(p + offset, &v, sizeof(v)); }

class Loader {
public:
    Loader(CMapDatabase* db, TextReader* reader) : db_(db), reader_(reader), map_(db->maps.size - 1) {}

    void Line();

private:
    void Split() { reader_->Split(&words_, 1); }
    const char* Word(int i) const { return words_[i]; }
    int Count() const { return words_.size_; }
    char* Map() { return reinterpret_cast<char*>(db_->maps.data) + map_ * map::kSize; }
    char* Exit() { return Body(Map() + map::kExits).data + exit_ * exit::kSize; }

    void AddAllod();
    void AddMap();
    bool MapLine(const char* word);
    void AddExit();
    void ExitLine(const char* word);

    CMapDatabase* db_;
    TextReader* reader_;
    int map_;          // the current map (starts at the last one already loaded)
    int exit_ = -1;    // the current exit of the current map
    console::CStringArray words_;
};

void Loader::AddAllod() {
    ArrayBody& allods = *reinterpret_cast<ArrayBody*>(&db_->allods.data);
    int index = allods.size;
    SetSize(allods, index + 1, allod::kSize, &ConstructAllods, &DestructAllods);
    char* a = allods.data + index * allod::kSize;
    StringAt(a, allod::kNames) = words_[1];
    for (int i = 1; i < 6; ++i) {
        Split();
        StringAt(allods.data + index * allod::kSize, allod::kNames + i * 4) = words_[0];
    }
}

void Loader::AddMap() {
    ArrayBody& maps = *reinterpret_cast<ArrayBody*>(&db_->maps.data);
    SetSize(maps, maps.size + 1, map::kSize, &ConstructMaps, &DestructMaps);
    ++map_;
    exit_ = -1;
    StringAt(Map(), map::kName) = words_[1];
    StringAt(Map(), map::kAllod) = words_[2];
    const char* allodName = words_[2];
    for (int i = 0; i < db_->allods.size; ++i) {
        char* a = reinterpret_cast<char*>(db_->allods.data) + i * allod::kSize;
        if (gamecrt::mbsicmp(*reinterpret_cast<const unsigned char* const*>(a), reinterpret_cast<const unsigned char*>(allodName)) != 0)
            continue;
        void* mapList = a + allod::kMaps;
        SetAtGrowDword(mapList, nullptr, Body(mapList).size, static_cast<std::uint32_t>(map_));
        break;
    }
    if (Is(Word(3), kGameText)) Set<int>(Map(), map::kKind, 0);
    else if (Is(Word(3), kBriefText)) Set<int>(Map(), map::kKind, 2);
    else if (Is(Word(3), kEdgeText)) Set<int>(Map(), map::kKind, 1);
    Set<int>(Map(), map::kWeather, 1);
    Set<int>(Map(), map::kCave, 0);
    Set<std::uint8_t>(Map(), map::kRestricted, 0);
    Set<std::uint8_t>(Map(), map::kCage, 0);
}

void Loader::AddExit() {
    ArrayBody& exits = Body(Map() + map::kExits);
    SetSize(exits, exits.size + 1, exit::kSize, &ConstructExits, &DestructExits);
    ++exit_;
    Set<int>(Exit(), exit::kView, 0);
    Set<std::uint8_t>(Exit(), exit::kHasDeployAngle, 0);
    Set<int>(Exit(), exit::kDeployAngle, 0);
    Split();
    if (Count() > 1) {
        StringAt(Exit(), exit::kTarget) = words_[0];
        Set<int>(Exit(), exit::kTargetIndex, atoi(Word(1)) - 1);
    }
}

// A line of the current map. Returns false if the word is no map directive.
bool Loader::MapLine(const char* word) {
    if (Is(word, kResText)) {
        Split();
        if (Count() > 1) {
            StringAt(Map(), map::kRes1) = words_[0];
            StringAt(Map(), map::kRes2) = words_[1];
        }
    } else if (Is(word, kCameraText)) {
        Split();
        if (Count() > 0) StringAt(Map(), map::kCamera) = words_[0];
    } else if (Is(word, kMapsText)) {
        Split();
        if (Count() > 1) {
            Set<int>(Map(), map::kSizeX, atoi(Word(0)));
            Set<int>(Map(), map::kSizeY, atoi(Word(1)));
        }
        Split();
        if (Count() > 1) {
            StringAt(Map(), map::kMaps1) = words_[0];
            StringAt(Map(), map::kMaps2) = words_[1];
        }
    } else if (Is(word, kFigureText)) {
        Split();
        if (Count() > 0) StringAt(Map(), map::kFigure) = words_[0];
    } else if (Is(word, kWeatherText)) {
        Split();
        if (Count() > 0) {
            if (Is(Word(0), kNoneText)) Set<int>(Map(), map::kWeather, 0);
            else if (Is(Word(0), kSnowText)) Set<int>(Map(), map::kWeather, 2);
            else if (Is(Word(0), kTornadoText)) Set<int>(Map(), map::kWeather, 3);
            else Set<int>(Map(), map::kWeather, 1);
        }
    } else if (Is(word, kCageText)) {
        Set<std::uint8_t>(Map(), map::kCage, 1);
    } else if (Is(word, kRestrictText)) {
        Split();
        if (Count() > 2) {
            Set<std::uint8_t>(Map(), map::kRestricted, 1);
            for (int i = 0; i < 3; ++i) Set<float>(Map(), map::kRestrict + i * 4, static_cast<float>(atof(Word(i))));
        }
    } else if (Is(word, kSkyText)) {
        Split();
        if (Count() > 0) Set<int>(Map(), map::kCave, Is(Word(0), kCaveText) ? 1 : 0);
    } else if (Is(word, kExitText)) {
        AddExit();
    } else {
        return false;
    }
    return true;
}

// A line of the current exit.
void Loader::ExitLine(const char* word) {
    bool deploy = Is(word, kDeployText);
    if (deploy || Is(word, kRemoveText)) {
        // "#deploy" sets both quadruples, "#remove" only the second.
        Split();
        if (Count() > 3) {
            for (int i = 0; i < 4; ++i) {
                float v = static_cast<float>(atof(Word(i)));
                Set<float>(Exit(), exit::kRemove + i * 4, v);
                if (deploy) Set<float>(Exit(), exit::kDeploy + i * 4, v);
            }
            Set<int>(Exit(), exit::kAngle, 0);
        }
        if (Count() > 4) StoreRadians(Exit() + exit::kAngle, Word(4));
    } else if (Is(word, kPassTimeText)) {
        Split();
        if (Count() > 0) Set<float>(Exit(), exit::kPassTime, static_cast<float>(atof(Word(0))));
    } else if (Is(word, kViewText)) {
        Split();
        words_.SetAtGrow(Count(), GameText(kZeroText));
        StoreRadians(Exit() + exit::kView, Word(0));
    } else if (Is(word, kDeployAngleText)) {
        Set<std::uint8_t>(Exit(), exit::kHasDeployAngle, 1);
        Split();
        words_.SetAtGrow(Count(), GameText(kZeroText));
        StoreRadians(Exit() + exit::kDeployAngle, Word(0));
    }
}

void Loader::Line() {
    Split();
    if (Is(Word(0), kAllodText) && Count() > 1) {
        AddAllod();
    } else if (Is(Word(0), kZoneText) && Count() > 3) {
        AddMap();
    } else if (map_ >= 0) {
        if (!MapLine(Word(0)) && exit_ >= 0) ExitLine(Word(0));
    }
}

} // namespace
} // namespace mapdb

bool CMapDatabase::Load(const char* fileName) {
    using namespace mapdb;
    TextReader* reader = TheTextReader();
    Loader loader(this, reader);
    if (!reader->Open(fileName)) return false;
    while (reader->NextLine(0)) loader.Line();
    reader->Close();
    return true;
}
EI_REPLACE_METHOD(0x0045EA20, CMapDatabase::Load);
