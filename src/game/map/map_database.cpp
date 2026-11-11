// The map database loader: MapDatabase's allods, maps and exits (map/map_database.h) filled from map.txt / map-
// LMP.txt; ZoneExit's area test and stream form.
// More: docs/notes/objects.md, "Map database".
#include "game/map/map_database.h"

#include "game/app/singletons.h"
#include "game/console/console.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "engine/data/text_manager.h"
#include "game/saves/save_versions.h"
#include "game/world/world.h"

#include "lib/maths.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace mapdb {
namespace {

// *out = text's degrees in radians.
void StoreRadians(float* out, std::string_view text) { *out = ei::kRadiansPerDegree * str::ToFloat(text); }

bool Is(std::string_view word, std::string_view text) { return str::Compare(word, text) == 0; }

class Loader {
public:
    Loader(MapDatabase* newDb, TextManager* newReader) : db(newDb), reader(newReader), map(static_cast<int>(newDb->maps.size()) - 1) {}

    void Line();

private:
    void Split() { words = reader->Split(true); }
    const std::string& Word(int i) const { return words[i]; }
    int Count() const { return static_cast<int>(words.size()); }
    MapRecord& Map() { return db->maps[map]; }
    ZoneExit& Exit() { return Map().exits[exit]; }

    void AddAllod();
    void AddMap();
    bool MapLine(const std::string& word);
    void AddExit();
    void ExitLine(const std::string& word);

    MapDatabase* db = nullptr;
    TextManager* reader = nullptr;
    int map = 0;       // the current map (starts at the last one already loaded)
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
    if (Is(Word(3), "game")) Map().kind = 0;
    else if (Is(Word(3), "brief")) Map().kind = 2;
    else if (Is(Word(3), "edge")) Map().kind = 1;
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
        Exit().entrance = str::ToInt(Word(1)) - 1;
    }
}

// A line of the current map. Returns false if the word is no map directive.
bool Loader::MapLine(const std::string& word) {
    if (Is(word, "#res")) {
        Split();
        if (Count() > 1) {
            Map().map = words[0];
            Map().mob = words[1];
        }
    } else if (Is(word, "#camera")) {
        Split();
        if (Count() > 0) Map().camera = words[0];
    } else if (Is(word, "#maps")) {
        Split();
        if (Count() > 1) {
            Map().sizeX = str::ToInt(Word(0));
            Map().sizeY = str::ToInt(Word(1));
        }
        Split();
        if (Count() > 1) {
            Map().picture = words[0];
            Map().picture2 = words[1];
        }
    } else if (Is(word, "#figure")) {
        Split();
        if (Count() > 0) Map().figure = words[0];
    } else if (Is(word, "#weather")) {
        Split();
        if (Count() > 0) {
            if (Is(Word(0), "none")) Map().climate = 0;
            else if (Is(Word(0), "snow")) Map().climate = 2;
            else if (Is(Word(0), "tornado")) Map().climate = 3;
            else Map().climate = 1;
        }
    } else if (Is(word, "#cage")) {
        Map().cage = true;
    } else if (Is(word, "#restrict")) {
        Split();
        if (Count() > 2) {
            Map().restricted = true;
            Map().restrictX = str::ToFloat(Word(0));
            Map().restrictY = str::ToFloat(Word(1));
            Map().restrictRadius = str::ToFloat(Word(2));
        }
    } else if (Is(word, "#sky")) {
        Split();
        if (Count() > 0) Map().cave = Is(Word(0), "cave") ? 1 : 0;
    } else if (Is(word, "#exit")) {
        AddExit();
    } else {
        return false;
    }
    return true;
}

// A line of the current exit.
void Loader::ExitLine(const std::string& word) {
    bool deploy = Is(word, "#deploy");
    if (deploy || Is(word, "#remove")) {
        // "#deploy" sets both quadruples, "#remove" only the second.
        Split();
        if (Count() > 3) {
            // Word i to the area's i-th coordinate (x1, y1, x2, y2), and to deploy[i].
            auto put = [&](int i, float& area) {
                float v = str::ToFloat(Word(i));
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
    } else if (Is(word, "#passtime")) {
        Split();
        if (Count() > 0) Exit().passTime = str::ToFloat(Word(0));
    } else if (Is(word, "#view")) {
        Split();
        words.push_back("0");
        StoreRadians(&Exit().view, Word(0));
    } else if (Is(word, "#deployangle")) {
        Exit().hasDeployAngle = 1;
        Split();
        words.push_back("0");
        StoreRadians(&Exit().deployAngle, Word(0));
    }
}

void Loader::Line() {
    Split();
    if (Is(Word(0), "#allod") && Count() > 1) {
        AddAllod();
    } else if (Is(Word(0), "#zone") && Count() > 3) {
        AddMap();
    } else if (map >= 0) {
        if (!MapLine(Word(0)) && exit >= 0) ExitLine(Word(0));
    }
}

} // namespace
} // namespace mapdb

bool MapDatabase::Load(std::string_view fileName) {
    using namespace mapdb;
    TextManager* reader = g_clientWorld.texts.get();
    Loader loader(this, reader);
    if (!reader->Open(fileName)) return false;
    while (reader->FindHeader()) loader.Line();
    reader->Close();
    return true;
}

// The map database file names ("map.txt", "map-LMP.txt").
bool MapDatabase::LoadAll() {
    allods.clear();
    maps.clear();
    Load("map.txt");
    return Load("map-LMP.txt");
}

// The map database (a plain global: the C++ runtime builds it empty and destroys it).
MapDatabase g_mapDatabase;

MapRecord* MapDatabase::FindMap(std::string_view name) {
    for (MapRecord& map : maps)
        if (text::CompareIgnoringCase(map.name, name) == 0) return &map;
    return nullptr;
}

AllodRecord* MapDatabase::FindAllod(std::string_view name) {
    for (AllodRecord& allod : allods)
        if (text::CompareIgnoringCase(allod.name, name) == 0) return &allod;
    return nullptr;
}

// --- ZoneExit ----------------------------------------------------------------------------------------

// The zone exit's area test (world.h, ZoneExit). The rectangle is turned by `angle` about its centre: the point is
// turned the other way first. Comparisons keep the game's NaN cases: a NaN angle takes the axis-aligned path, a NaN
// turned x is outside, the other tests let NaN in. Also the exit's stream form and the stream's string reader.
bool ZoneExit::Contains(float x, float y) const {
    if (angle == 0.0f) {
        if (x1 > x) return false;
        if (x > x2) return false;
        if (y1 > y) return false;
        return y <= y2;
    }
    const float centerX = (x2 + x1) * 0.5f;
    const float centerY = (y2 + y1) * 0.5f;
    const float c = lib::Cos(angle);
    const float s = lib::Sin(angle);
    const float dy = y - centerY;
    const float dx = x - centerX;
    const float turnedX = dx * c + dy * s + centerX;
    const float turnedY = centerY - dx * s + dy * c;
    if (turnedX < x1) return false;
    if (turnedX > x2) return false;
    if (y1 > turnedY) return false;
    return turnedY <= y2;
}

void ZoneExit::Read(ByteStream* stream) {
    stream->ReadValue(passTime);
    for (float& value : deploy) stream->ReadValue(value);
    stream->ReadValue(x1);
    stream->ReadValue(y1);
    stream->ReadValue(x2);
    stream->ReadValue(y2);
    target = stream->ReadString();
    stream->ReadValue(entrance);
    stream->ReadValue(view);
    hasDeployAngle = 0;
    deployAngle = 0;
    angle = 0;
    if (stream->version >= kVersionExitExtras) {
        stream->ReadValue(hasDeployAngle);
        stream->ReadValue(deployAngle);
        stream->ReadValue(angle);
    }
}

void ZoneExit::Write(ByteStream* stream) const {
    auto put = [stream](const void* field, std::uint32_t size) {
        std::uint32_t copy = 0;
        std::memcpy(&copy, field, size);
        stream->Write(&copy, size);
    };
    put(&passTime, 4);
    for (const float& value : deploy) put(&value, 4);
    put(&x1, 4);
    put(&y1, 4);
    put(&x2, 4);
    put(&y2, 4);
    stream->WriteString(target);
    put(&entrance, 4);
    put(&view, 4);
    put(&hasDeployAngle, 1);
    put(&deployAngle, 4);
    put(&angle, 4);
}
