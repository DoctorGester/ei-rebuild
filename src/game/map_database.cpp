// The map database loader (0045EA20): CMapDatabase's allods, maps and exits (object_tables.h) filled
// from map.txt / map-LMP.txt.
//
// The file is read line by line through the text manager (007AF788) and split into words. Lines
// starting with a directive word set the current record:
//   #allod <name>, then 5 lines with one word each (the allod's other names)
//   #zone <name> <allod> <game|brief|edge>       a new map, the current one from here
//   #res, #camera, #maps, #figure (each followed by a line of words), #weather, #cage, #restrict,
//   #sky                                         fields of the current map
//   #exit (then "<target> <n>")                  a new exit of the current map
//   #deploy, #remove, #passtime, #view, #deployangle   fields of the current exit
#include "item_db_text.h"
#include "angles.h"
#include "game_strings.h"

#include "console.h"
#include "object_tables.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "text_reader.h"
#include "game_arrays.h"
#include "game_slots.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>
#include <type_traits>

#pragma warning(disable : 4740) // naked forwarders

namespace mapdb {
namespace {

// The text manager (item_db_text.h) as the loader uses it. Not rewritten.
struct TextReader : CTextManager {
    mfc::BOOL Open(const char* name);                          // 004DEFC0: finds the text, cursor at its start
    mfc::BOOL NextHeader();                                    // 004DF030 (null key): the next "#" line; false at the end
    int Split(console::CStringArray* words, int flag);         // 004DFCB0: the current line's words
    mfc::BOOL Close();                                         // 004DF020
};
mfc::BOOL TextReader::Open(const char* key) { return ::TextsOpenSection(this, nullptr, key); }
mfc::BOOL TextReader::NextHeader() { return ::TextFindLineOriginal(this, nullptr, nullptr); }
int TextReader::Split(console::CStringArray* words, int flag) {
    return CTextManager::Split(words, flag);
}
mfc::BOOL TextReader::Close() { return TextClose(this); }
TextReader* TheTextReader() { return static_cast<TextReader*>(TheTextManager()); }

// Element constructors / destructors of the record arrays: the allods' (00460720 / 004606F0), the
// maps' (004607E0 / 004607B0) and the exits' (004606A0 / 00460670) instantiations.
using ElementFn = void(__stdcall*)(void* elements, int count);
constexpr ElementFn ConstructAllods = mfc::impl::ConstructElementsAt<mfc::impl::Allods>;
constexpr ElementFn DestructAllods = mfc::impl::DestructElementsAt<mfc::impl::OwnedAllods>;
constexpr ElementFn ConstructMaps = mfc::impl::ConstructElementsAt<mfc::impl::MapRecords>;
constexpr ElementFn DestructMaps = mfc::impl::DestructElementsAt<gamearrays::ZoneRecords>;
constexpr ElementFn ConstructExits = mfc::impl::ConstructElementsAt<mfc::impl::Exits3C>;
constexpr ElementFn DestructExits = mfc::impl::DestructElementsAt<gamearrays::Exits>;

// *out = float(GameAtof(text) * (pi * 1/180)): degrees to radians on the FPU as the original does
// (the factor is the product of two doubles at 0073C3B8 and 0073BA30, rounded by the FPU).
void StoreRadians(float* out, const char* text) {
    const double degrees = GameAtof(text);
    *out = static_cast<float>(degrees * ei::kRadiansPerDegree);
}

bool Is(const char* word, const char* text) {
    return gamecrt::mbscmp(word, text) == 0;
}


// CArray::SetSize(size, -1) as the original inlines it, with the instantiation's element
// constructor and destructor.
template <typename Array>
void SetSize(Array& a, int size, ElementFn construct, ElementFn destruct) {
    using T = std::remove_pointer_t<decltype(a.data)>;
    if (size == 0) {
        if (a.data) {
            destruct(a.data, a.size);
            gamecrt::operator_delete(a.data);
            a.data = nullptr;
        }
        a.maxSize = 0;
        a.size = 0;
    } else if (!a.data) {
        a.data = gamecrt::operator_new_array<T>(size);
        construct(a.data, size);
        a.maxSize = size;
        a.size = size;
    } else if (size <= a.maxSize) {
        if (size > a.size) construct(a.data + a.size, size - a.size);
        else if (size < a.size) destruct(a.data + size, a.size - size);
        a.size = size;
    } else {
        int grow = a.growBy;
        if (grow == 0) {
            grow = a.size / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int capacity = a.maxSize + grow;
        if (size >= capacity) capacity = size;
        T* data = gamecrt::operator_new_array<T>(capacity);
        std::memcpy(static_cast<void*>(data), a.data, a.size * sizeof(T));
        construct(data + a.size, size - a.size);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.size = size;
        a.maxSize = capacity;
    }
}

class Loader {
public:
    Loader(CMapDatabase* db, TextReader* reader) : db_(db), reader_(reader), map_(db->maps.size - 1) {}

    void Line();

private:
    void Split() { reader_->Split(&words_, 1); }
    const char* Word(int i) const { return words_[i]; }
    int Count() const { return words_.size; }
    MapRecord& Map() { return db_->maps.data[map_]; }
    ZoneExit& Exit() { return Map().exits.data[exit_]; }

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
    mfc::CArray<AllodRecord>& allods = db_->allods;
    int index = allods.size;
    SetSize(allods, index + 1, ConstructAllods, DestructAllods);
    allods.data[index].name = words_[1];
    for (int i = 0; i < 5; ++i) {
        Split();
        allods.data[index].otherNames[i] = words_[0];
    }
}

void Loader::AddMap() {
    SetSize(db_->maps, db_->maps.size + 1, ConstructMaps, DestructMaps);
    ++map_;
    exit_ = -1;
    Map().name = words_[1];
    Map().allod = words_[2];
    const char* allodName = words_[2];
    for (int i = 0; i < db_->allods.size; ++i) {
        AllodRecord& allod = db_->allods.data[i];
        if (gamecrt::mbsicmp(allod.name.GetString(), allodName) != 0) continue;
        allod.maps.SetAtGrow(allod.maps.size, map_);   // 00460500
        break;
    }
    if (Is(Word(3), game::mapwords::Game())) Map().kind = 0;
    else if (Is(Word(3), game::mapwords::Brief())) Map().kind = 2;
    else if (Is(Word(3), game::mapwords::Edge())) Map().kind = 1;
    Map().setting2C = 1;   // the weather
    Map().setting30 = 0;   // no cave
    Map().restricted = false;
    Map().cage = false;
}

void Loader::AddExit() {
    ZoneExits& exits = Map().exits;
    SetSize(exits, exits.size + 1, ConstructExits, DestructExits);
    ++exit_;
    Exit().view = 0.0f;
    Exit().hasDeployAngle = 0;
    Exit().deployAngle = 0.0f;
    Split();
    if (Count() > 1) {
        Exit().target = words_[0];
        Exit().entrance = libcrt::Atoi(Word(1)) - 1;
    }
}

// A line of the current map. Returns false if the word is no map directive.
bool Loader::MapLine(const char* word) {
    if (Is(word, game::mapwords::Res())) {
        Split();
        if (Count() > 1) {
            Map().map = words_[0];
            Map().mob = words_[1];
        }
    } else if (Is(word, game::mapwords::Camera())) {
        Split();
        if (Count() > 0) Map().camera = words_[0];
    } else if (Is(word, game::mapwords::Maps())) {
        Split();
        if (Count() > 1) {
            Map().sizeX = libcrt::Atoi(Word(0));
            Map().sizeY = libcrt::Atoi(Word(1));
        }
        Split();
        if (Count() > 1) {
            Map().picture = words_[0];
            Map().picture2 = words_[1];
        }
    } else if (Is(word, game::mapwords::Figure())) {
        Split();
        if (Count() > 0) Map().figure = words_[0];
    } else if (Is(word, game::mapwords::Weather())) {
        Split();
        if (Count() > 0) {
            if (Is(Word(0), game::NoneText())) Map().setting2C = 0;
            else if (Is(Word(0), game::mapwords::Snow())) Map().setting2C = 2;
            else if (Is(Word(0), game::mapwords::Tornado())) Map().setting2C = 3;
            else Map().setting2C = 1;
        }
    } else if (Is(word, game::mapwords::Cage())) {
        Map().cage = true;
    } else if (Is(word, game::mapwords::Restrict())) {
        Split();
        if (Count() > 2) {
            Map().restricted = true;
            Map().restrictX = static_cast<float>(GameAtof(Word(0)));
            Map().restrictY = static_cast<float>(GameAtof(Word(1)));
            Map().restrictRadius = static_cast<float>(GameAtof(Word(2)));
        }
    } else if (Is(word, game::mapwords::Sky())) {
        Split();
        if (Count() > 0) Map().setting30 = Is(Word(0), game::mapwords::Cave()) ? 1 : 0;
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
            // Word i to the area's i-th coordinate (x1, y1, x2, y2), and to deploy[i].
            auto put = [&](int i, float& area) {
                float v = static_cast<float>(GameAtof(Word(i)));
                area = v;
                if (deploy) Exit().deploy[i] = v;
            };
            put(0, Exit().x1);
            put(1, Exit().y1);
            put(2, Exit().x2);
            put(3, Exit().y2);
            Exit().angle = 0.0f;
        }
        if (Count() > 4) StoreRadians(&Exit().angle, Word(4));
    } else if (Is(word, game::mapwords::PassTime())) {
        Split();
        if (Count() > 0) Exit().passTime = static_cast<float>(GameAtof(Word(0)));
    } else if (Is(word, game::mapwords::View())) {
        Split();
        words_.SetAtGrow(Count(), game::mapwords::Zero());
        StoreRadians(&Exit().view, Word(0));
    } else if (Is(word, game::mapwords::DeployAngle())) {
        Exit().hasDeployAngle = 1;
        Split();
        words_.SetAtGrow(Count(), game::mapwords::Zero());
        StoreRadians(&Exit().deployAngle, Word(0));
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
EI_REWRITTEN(0x0045EA20, CMapDatabase::Load);

// 007856A0 "map.txt", 00785694 "map-LMP.txt".
bool CMapDatabase::LoadAll() {
    Load("map.txt");
    return Load("map-LMP.txt");
}
EI_REWRITTEN(0x0045EA00, CMapDatabase::LoadAll);

CMapDatabase* CMapDatabase::Construct() {
    allods.Construct(mfc::impl::AllodArrayClass::Vtable());
    maps.Construct(gameslots::MapRecordArrayClass::Vtable());
    return this;
}
EI_REPLACE_METHOD(0x0045E820, CMapDatabase::Construct);
