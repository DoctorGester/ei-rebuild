// The map database loader: MapDatabase's allods, maps and exits (object_tables.h) filled from map.txt / map-
// LMP.txt.
// More: docs/notes/comments-c22i.md, "map_database.cpp".
#include "angles.h"
#include "console.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "text_manager.h"
#include "object_tables.h"
#include "world.h"

#include "lib/pc24.h"
#include "lib/text_case.h"

#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace mapdb {
namespace {

// *out = std::atof(text) * (pi * 1/180): degrees to radians, the double times the float factor rounded once to a
// float as the original's multiply at 24-bit precision (the map database loads after the device exists).
void StoreRadians(float* out, const char* text) { *out = pc24::Mul(ei::kRadiansPerDegree, std::atof(text)); }

bool Is(const char* word, const char* text) {
    return std::strcmp(word, text) == 0;
}

class Loader {
public:
    Loader(MapDatabase* newDb, TextManager* newReader) : db(newDb), reader(newReader), map(static_cast<int>(newDb->maps.size()) - 1) {}

    void Line();

private:
    void Split() { words = reader->Split(true); }
    const char* Word(int i) const { return words[i].c_str(); }
    int Count() const { return static_cast<int>(words.size()); }
    MapRecord& Map() { return db->maps[map]; }
    ZoneExit& Exit() { return Map().exits[exit]; }

    void AddAllod();
    void AddMap();
    bool MapLine(const char* word);
    void AddExit();
    void ExitLine(const char* word);

    MapDatabase* db;
    TextManager* reader;
    int map;           // the current map (starts at the last one already loaded)
    int exit = -1;     // the current exit of the current map
    std::vector<std::string> words;
};

void Loader::AddAllod() {
    AllodRecord& allod = db->allods.emplace_back();
    allod.name = words[1];
    for (int i = 0; i < 5; ++i) {
        Split();
        allod.otherNames[i] = words[0];
    }
}

void Loader::AddMap() {
    db->maps.emplace_back();
    ++map;
    exit = -1;
    Map().name = words[1];
    Map().allod = words[2];
    for (AllodRecord& allod : db->allods) {
        if (text::CompareIgnoringCase(allod.name, words[2]) != 0) continue;
        allod.maps.push_back(map);
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
    ++exit;
    Exit().view = 0.0f;
    Exit().hasDeployAngle = 0;
    Exit().deployAngle = 0.0f;
    Split();
    if (Count() > 1) {
        Exit().target = words[0];
        Exit().entrance = std::atoi(Word(1)) - 1;
    }
}

// A line of the current map. Returns false if the word is no map directive.
bool Loader::MapLine(const char* word) {
    if (Is(word, game::mapwords::Res())) {
        Split();
        if (Count() > 1) {
            Map().map = words[0];
            Map().mob = words[1];
        }
    } else if (Is(word, game::mapwords::Camera())) {
        Split();
        if (Count() > 0) Map().camera = words[0];
    } else if (Is(word, game::mapwords::Maps())) {
        Split();
        if (Count() > 1) {
            Map().sizeX = std::atoi(Word(0));
            Map().sizeY = std::atoi(Word(1));
        }
        Split();
        if (Count() > 1) {
            Map().picture = words[0];
            Map().picture2 = words[1];
        }
    } else if (Is(word, game::mapwords::Figure())) {
        Split();
        if (Count() > 0) Map().figure = words[0];
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
        words.push_back(game::mapwords::Zero());
        StoreRadians(&Exit().view, Word(0));
    } else if (Is(word, game::mapwords::DeployAngle())) {
        Exit().hasDeployAngle = 1;
        Split();
        words.push_back(game::mapwords::Zero());
        StoreRadians(&Exit().deployAngle, Word(0));
    }
}

void Loader::Line() {
    Split();
    if (Is(Word(0), game::mapwords::Allod()) && Count() > 1) {
        AddAllod();
    } else if (Is(Word(0), game::mapwords::Zone()) && Count() > 3) {
        AddMap();
    } else if (map >= 0) {
        if (!MapLine(Word(0)) && exit >= 0) ExitLine(Word(0));
    }
}

} // namespace
} // namespace mapdb

bool MapDatabase::Load(const char* fileName) {
    using namespace mapdb;
    TextManager* reader = g_clientWorld.texts;
    Loader loader(this, reader);
    if (!reader->Open(fileName)) return false;
    while (reader->FindHeader()) loader.Line();
    reader->Close();
    return true;
}

// The map database file names ("map.txt", "map-LMP.txt").
bool MapDatabase::LoadAll() {
    Load("map.txt");
    return Load("map-LMP.txt");
}

// The map database (a plain global: the C++ runtime builds it empty and destroys it).
MapDatabase g_mapDatabase;
