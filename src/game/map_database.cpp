// The map database loader: CMapDatabase's allods, maps and exits (object_tables.h) filled from map.txt / map-
// LMP.txt.
// More: docs/notes/comments-c22i.md, "map_database.cpp".
#include "angles.h"
#include "console.h"
#include "game_data.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item_db_text.h"
#include "object_tables.h"
#include "world.h"

#include "lib/text_case.h"

#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace mapdb {
namespace {

// *out = float(std::atof(text) * (pi * 1/180)): degrees to radians on the FPU as the original does (the
// factor is the product of two doubles, rounded by the FPU).
void StoreRadians(float* out, const char* text) {
    const double degrees = std::atof(text);
    *out = static_cast<float>(degrees * ei::kRadiansPerDegree);
}

bool Is(const char* word, const char* text) {
    return std::strcmp(word, text) == 0;
}

class Loader {
public:
    Loader(CMapDatabase* db, CTextManager* reader) : db_(db), reader_(reader), map_(static_cast<int>(db->maps.size()) - 1) {}

    void Line();

private:
    void Split() { reader_->Split(&words_, true); }
    const char* Word(int i) const { return words_[i].c_str(); }
    int Count() const { return static_cast<int>(words_.size()); }
    MapRecord& Map() { return db_->maps[map_]; }
    ZoneExit& Exit() { return Map().exits[exit_]; }

    void AddAllod();
    void AddMap();
    bool MapLine(const char* word);
    void AddExit();
    void ExitLine(const char* word);

    CMapDatabase* db_;
    CTextManager* reader_;
    int map_;          // the current map (starts at the last one already loaded)
    int exit_ = -1;    // the current exit of the current map
    std::vector<std::string> words_;
};

void Loader::AddAllod() {
    AllodRecord& allod = db_->allods.emplace_back();
    allod.name = words_[1];
    for (int i = 0; i < 5; ++i) {
        Split();
        allod.otherNames[i] = words_[0];
    }
}

void Loader::AddMap() {
    db_->maps.emplace_back();
    ++map_;
    exit_ = -1;
    Map().name = words_[1];
    Map().allod = words_[2];
    for (AllodRecord& allod : db_->allods) {
        if (text::CompareIgnoringCase(allod.name, words_[2]) != 0) continue;
        allod.maps.push_back(map_);
        break;
    }
    if (Is(Word(3), game::mapwords::Game())) Map().kind = 0;
    else if (Is(Word(3), game::mapwords::Brief())) Map().kind = 2;
    else if (Is(Word(3), game::mapwords::Edge())) Map().kind = 1;
    Map().climate = 1;   // the weather
    Map().cave = 0;   // no cave
    Map().restricted = false;
    Map().cage = false;
}

void Loader::AddExit() {
    Map().exits.emplace_back();
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
            if (Is(Word(0), game::NoneText())) Map().climate = 0;
            else if (Is(Word(0), game::mapwords::Snow())) Map().climate = 2;
            else if (Is(Word(0), game::mapwords::Tornado())) Map().climate = 3;
            else Map().climate = 1;
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
        if (Count() > 0) Map().cave = Is(Word(0), game::mapwords::Cave()) ? 1 : 0;
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
    CTextManager* reader = g_clientWorld.texts_;
    Loader loader(this, reader);
    if (!reader->Open(fileName)) return false;
    while (reader->FindHeader()) loader.Line();
    reader->Close();
    return true;
}

// The map database file names ("map.txt", "map-LMP.txt").
bool CMapDatabase::LoadAll() {
    Load("map.txt");
    return Load("map-LMP.txt");
}

// The map database (a plain global: the C++ runtime builds it empty and destroys it).
CMapDatabase g_mapDatabase;
