// The map database loader (0045EA20). See map_database.h for the file format.
#include "map_database.h"
#include "game_strings.h"

#include "console.h"
#include "object_tables.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_ui.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace mapdb {
namespace {

// The text manager (item_db_text.h) as the loader uses it. Not rewritten.
struct TextReader {
    mfc::BOOL Open(const char* name);                          // 004DEFC0: finds the text, cursor at its start
    mfc::BOOL NextHeader();                                    // 004DF030 (null key): the next "#" line; false at the end
    int Split(console::CStringArray* words, int flag);         // 004DFCB0: the current line's words
    mfc::BOOL Close();                                         // 004DF020
};
mfc::BOOL TextReader::Open(const char* key) { return ::TextsOpenSection(this, nullptr, key); }
mfc::BOOL TextReader::NextHeader() { return ::TextFindLineOriginal(this, nullptr, nullptr); }
int TextReader::Split(console::CStringArray* words, int flag) {
    return ReaderSplit(reinterpret_cast<CTextManager*>(this), nullptr, words, flag);
}
mfc::BOOL TextReader::Close() { return ReaderClose(reinterpret_cast<CTextManager*>(this)); }
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
void __fastcall SetAtGrowDword(void* a0, void*, int a2, std::uint32_t a3) { DwordArraySetAtGrowOriginal(a0, nullptr, a2, a3); }

// *out = float(GameAtof(text) * (pi * 1/180)): degrees to radians on the FPU as the original does
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

bool Is(const char* word, const char* text) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(word), reinterpret_cast<const unsigned char*>(text)) == 0;
}


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
    int Count() const { return words_.size; }
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
    if (Is(Word(3), game::mapwords::Game())) Set<int>(Map(), map::kKind, 0);
    else if (Is(Word(3), game::mapwords::Brief())) Set<int>(Map(), map::kKind, 2);
    else if (Is(Word(3), game::mapwords::Edge())) Set<int>(Map(), map::kKind, 1);
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
        Set<int>(Exit(), exit::kTargetIndex, GameAtoi(Word(1)) - 1);
    }
}

// A line of the current map. Returns false if the word is no map directive.
bool Loader::MapLine(const char* word) {
    if (Is(word, game::mapwords::Res())) {
        Split();
        if (Count() > 1) {
            StringAt(Map(), map::kRes1) = words_[0];
            StringAt(Map(), map::kRes2) = words_[1];
        }
    } else if (Is(word, game::mapwords::Camera())) {
        Split();
        if (Count() > 0) StringAt(Map(), map::kCamera) = words_[0];
    } else if (Is(word, game::mapwords::Maps())) {
        Split();
        if (Count() > 1) {
            Set<int>(Map(), map::kSizeX, GameAtoi(Word(0)));
            Set<int>(Map(), map::kSizeY, GameAtoi(Word(1)));
        }
        Split();
        if (Count() > 1) {
            StringAt(Map(), map::kMaps1) = words_[0];
            StringAt(Map(), map::kMaps2) = words_[1];
        }
    } else if (Is(word, game::mapwords::Figure())) {
        Split();
        if (Count() > 0) StringAt(Map(), map::kFigure) = words_[0];
    } else if (Is(word, game::mapwords::Weather())) {
        Split();
        if (Count() > 0) {
            if (Is(Word(0), game::NoneText())) Set<int>(Map(), map::kWeather, 0);
            else if (Is(Word(0), game::mapwords::Snow())) Set<int>(Map(), map::kWeather, 2);
            else if (Is(Word(0), game::mapwords::Tornado())) Set<int>(Map(), map::kWeather, 3);
            else Set<int>(Map(), map::kWeather, 1);
        }
    } else if (Is(word, game::mapwords::Cage())) {
        Set<std::uint8_t>(Map(), map::kCage, 1);
    } else if (Is(word, game::mapwords::Restrict())) {
        Split();
        if (Count() > 2) {
            Set<std::uint8_t>(Map(), map::kRestricted, 1);
            for (int i = 0; i < 3; ++i) Set<float>(Map(), map::kRestrict + i * 4, static_cast<float>(GameAtof(Word(i))));
        }
    } else if (Is(word, game::mapwords::Sky())) {
        Split();
        if (Count() > 0) Set<int>(Map(), map::kCave, Is(Word(0), game::mapwords::Cave()) ? 1 : 0);
    } else if (Is(word, game::mapwords::Exit())) {
        AddExit();
    } else {
        return false;
    }
    return true;
}

// A line of the current exit.
void Loader::ExitLine(const char* word) {
    bool deploy = Is(word, game::mapwords::Deploy());
    if (deploy || Is(word, game::mapwords::Remove())) {
        // "#deploy" sets both quadruples, "#remove" only the second.
        Split();
        if (Count() > 3) {
            for (int i = 0; i < 4; ++i) {
                float v = static_cast<float>(GameAtof(Word(i)));
                Set<float>(Exit(), exit::kRemove + i * 4, v);
                if (deploy) Set<float>(Exit(), exit::kDeploy + i * 4, v);
            }
            Set<int>(Exit(), exit::kAngle, 0);
        }
        if (Count() > 4) StoreRadians(Exit() + exit::kAngle, Word(4));
    } else if (Is(word, game::mapwords::PassTime())) {
        Split();
        if (Count() > 0) Set<float>(Exit(), exit::kPassTime, static_cast<float>(GameAtof(Word(0))));
    } else if (Is(word, game::mapwords::View())) {
        Split();
        words_.SetAtGrow(Count(), game::mapwords::Zero());
        StoreRadians(Exit() + exit::kView, Word(0));
    } else if (Is(word, game::mapwords::DeployAngle())) {
        Set<std::uint8_t>(Exit(), exit::kHasDeployAngle, 1);
        Split();
        words_.SetAtGrow(Count(), game::mapwords::Zero());
        StoreRadians(Exit() + exit::kDeployAngle, Word(0));
    }
}

void Loader::Line() {
    Split();
    if (Is(Word(0), game::mapwords::Allod()) && Count() > 1) {
        AddAllod();
    } else if (Is(Word(0), game::mapwords::Zone()) && Count() > 3) {
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
    while (reader->NextHeader()) loader.Line();
    reader->Close();
    return true;
}
EI_REPLACE_METHOD(0x0045EA20, CMapDatabase::Load);

// 007856A0 "map.txt", 00785694 "map-LMP.txt".
bool CMapDatabase::LoadAll() {
    Load(reinterpret_cast<const char*>(0x007856A0));
    return Load(reinterpret_cast<const char*>(0x00785694));
}
EI_REPLACE_METHOD(0x0045EA00, CMapDatabase::LoadAll);

CMapDatabase* CMapDatabase::Construct() {
    allods.Construct(kAllodArrayVtable);
    maps.Construct(kMapArrayVtable);
    return this;
}
EI_REPLACE_METHOD(0x0045E820, CMapDatabase::Construct);
