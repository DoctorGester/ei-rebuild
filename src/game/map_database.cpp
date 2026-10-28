// The map database loader: CMapDatabase's allods, maps and exits (object_tables.h) filled from map.txt / map-
// LMP.txt.
// More: docs/notes/comments-c22i.md, "map_database.cpp".
#include "item_db_text.h"
#include "angles.h"
#include "game_strings.h"

#include "console.h"
#include "object_tables.h"

#include "../lib/crt.h"
#include "lib/crt.h"
#include "text_reader.h"
#include "game_arrays.h"
#include "game_slots.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace mapdb {
namespace {

// The text manager (item_db_text.h) as the loader uses it. Not rewritten.
struct TextReader : CTextManager {
    mfc::BOOL Open(const char* name);                          // finds the text, cursor at its start
    mfc::BOOL NextHeader();                                    // (null key): the next "#" line; false at the end
    int Split(std::vector<std::string>* words, int flag);         // the current line's words
    mfc::BOOL Close();
};
mfc::BOOL TextReader::Open(const char* key) { return CTextManager::Open(key); }
mfc::BOOL TextReader::NextHeader() { return FindLine(nullptr); }
int TextReader::Split(std::vector<std::string>* words, int flag) {
    return CTextManager::Split(words, flag);
}
mfc::BOOL TextReader::Close() { return TextClose(this); }
TextReader* TheTextReader() { return static_cast<TextReader*>(TheTextManager()); }

// *out = float(std::atof(text) * (pi * 1/180)): degrees to radians on the FPU as the original does (the
// factor is the product of two doubles, rounded by the FPU).
void StoreRadians(float* out, const char* text) {
    const double degrees = std::atof(text);
    *out = static_cast<float>(degrees * ei::kRadiansPerDegree);
}

bool Is(const char* word, const char* text) {
    return gamecrt::mbscmp(word, text) == 0;
}

class Loader {
public:
    Loader(CMapDatabase* db, TextReader* reader) : db_(db), reader_(reader), map_(db->maps.size - 1) {}

    void Line();

private:
    void Split() { reader_->Split(&words_, 1); }
    const char* Word(int i) const { return words_[i].c_str(); }
    int Count() const { return static_cast<int>(words_.size()); }
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
    std::vector<std::string> words_;
};

void Loader::AddAllod() {
    auto& allods = db_->allods;
    int index = allods.size;
    allods.SetSize(index + 1);
    allods.data[index].name = words_[1];
    for (int i = 0; i < 5; ++i) {
        Split();
        allods.data[index].otherNames[i] = words_[0];
    }
}

void Loader::AddMap() {
    db_->maps.SetSize(db_->maps.size + 1);
    ++map_;
    exit_ = -1;
    Map().name = words_[1];
    Map().allod = words_[2];
    const char* allodName = words_[2].c_str();
    for (int i = 0; i < db_->allods.size; ++i) {
        AllodRecord& allod = db_->allods.data[i];
        if (gamecrt::mbsicmp(allod.name, allodName) != 0) continue;
        allod.maps.SetAtGrow(allod.maps.size, map_);
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
    exits.SetSize(exits.size + 1);
    ++exit_;
    Exit().view = 0.0f;
    Exit().hasDeployAngle = 0;
    Exit().deployAngle = 0.0f;
    Split();
    if (Count() > 1) {
        Exit().target = words_[0];
        Exit().entrance = std::atoi(Word(1)) - 1;
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
            Map().sizeX = std::atoi(Word(0));
            Map().sizeY = std::atoi(Word(1));
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
            Map().restrictX = static_cast<float>(std::atof(Word(0)));
            Map().restrictY = static_cast<float>(std::atof(Word(1)));
            Map().restrictRadius = static_cast<float>(std::atof(Word(2)));
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
                float v = static_cast<float>(std::atof(Word(i)));
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
        if (Count() > 0) Exit().passTime = static_cast<float>(std::atof(Word(0)));
    } else if (Is(word, game::mapwords::View())) {
        Split();
        words_.push_back(game::mapwords::Zero());
        StoreRadians(&Exit().view, Word(0));
    } else if (Is(word, game::mapwords::DeployAngle())) {
        Exit().hasDeployAngle = 1;
        Split();
        words_.push_back(game::mapwords::Zero());
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

// The map database file names ("map.txt", "map-LMP.txt").
bool CMapDatabase::LoadAll() {
    Load("map.txt");
    return Load("map-LMP.txt");
}

CMapDatabase* CMapDatabase::Construct() {
    allods.Construct(mfc::impl::AllodArrayClass::Vtable());
    maps.Construct(gameslots::MapRecordArrayClass::Vtable());
    return this;
}

// The map database, zero at the start: built by a static initialiser in the list's order and destroyed by the
// exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CMapDatabase> g_mapDatabase = /*0079BF00*/ {};
CMapDatabase* TheMapDatabase() { return &*g_mapDatabase; }

namespace {
void DestroyMapDatabase() { g_mapDatabase->Destruct(); }
} // namespace

void ConstructMapDatabase() {
    g_mapDatabase->Construct();
    std::atexit(DestroyMapDatabase);
}
