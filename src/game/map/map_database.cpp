// The map database loader: MapDatabase's allods, maps, exits and quests (map/map_database.h) filled from map.txt /
// map-LMP.txt, the map.toml files merged over them; ZoneExit's area test and stream form.
// More: docs/notes/objects.md, "Map database"; docs/notes/mods.md, "Zones from a mod".
#include "game/map/map_database.h"

#include "game/app/singletons.h"
#include "game/console/console.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "engine/data/database_toml.h"
#include "engine/data/resource.h"
#include "engine/data/text_manager.h"
#include "game/saves/save_versions.h"
#include "game/world/world.h"

#include "lib/maths.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/toml.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
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
    // Whether the cursor is at a line of values: not a header, not the text's end.
    bool AtValues() {
        const std::optional<std::string> word = reader->PeekWord();
        return word && !word->starts_with('#');
    }
    // The values line's x y z (missing numbers 0), none when the cursor is not at one.
    std::optional<Vector3> Position();

    void AddAllod();
    void AddMap();
    void AddQuest();
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
}

std::optional<Vector3> Loader::Position() {
    if (!AtValues()) return std::nullopt;
    Split();
    while (Count() < 3) words.push_back("0");
    return Vector3{str::ToFloat(Word(0)), str::ToFloat(Word(1)), str::ToFloat(Word(2))};
}

// "#quest name": its global map position on the next line, its places on the one after.
void Loader::AddQuest() {
    MapQuestRecord& quest = db->quests.emplace_back();
    quest.name = words[1];
    quest.position = Position();
    if (!AtValues()) return;
    Split();
    for (int i = 1; i < Count(); i += 2) quest.places.push_back({str::ToFloat(Word(i - 1)), str::ToFloat(Word(i))});
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
    } else if (Is(word, "#position")) {
        Map().position = Position();
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
    } else if (Is(Word(0), "#quest") && Count() > 1) {
        AddQuest();
    } else if (map >= 0) {
        if (!MapLine(Word(0)) && exit >= 0) ExitLine(Word(0));
    }
}

} // namespace
} // namespace mapdb

void MapDatabase::Load(std::string_view fileName) {
    using namespace mapdb;
    TextManager* reader = g_clientWorld.texts.get();
    Loader loader(this, reader);
    if (!reader->Open(fileName)) return;
    while (reader->FindHeader()) loader.Line();
    reader->Close();
}

// The map database file names ("map.txt", "map-LMP.txt"), then the map.toml files of texts.res's places (the text
// manager's first archive: Res\texts\ in each layer of the path map; a mod's over the game's).
void MapDatabase::LoadAll() {
    allods.clear();
    maps.clear();
    quests.clear();
    Load("map.txt");
    Load("map-LMP.txt");
    const std::vector<TextSource>& archives = g_clientWorld.texts->sources.archives;
    if (archives.empty()) return;
    const RecordFile file = archives.front().source->FindRecord("map");
    if (file.replaced) {
        allods.clear();
        maps.clear();
        quests.clear();
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
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

const MapQuestRecord* MapDatabase::FindQuest(std::string_view name) const {
    for (const MapQuestRecord& quest : quests)
        if (text::CompareIgnoringCase(quest.name, name) == 0) return &quest;
    return nullptr;
}

// --- As TOML (docs/notes/mods.md, "Zones from a mod") ------------------------------------------------------------

namespace mapdb {
namespace {

constexpr const char* kWhat = "map";

// A text as it is (UTF-8, as the text manager hands map.txt's out).
struct Text {
    std::string& value;
};
bool ReadValue(const toml::Value& value, Text field) { return toml::Read(value, &field.value); }

// An allod's five figure names.
struct Figures {
    std::string (&names)[5];
};
bool ReadValue(const toml::Value& value, Figures field) {
    if (!value.IsArray() || value.Size() != 5) return false;
    std::string read[5];
    for (int i = 0; i < 5; ++i)
        if (!toml::Read(value.At(i), &read[i])) return false;
    for (int i = 0; i < 5; ++i) field.names[i] = std::move(read[i]);
    return true;
}

// [x, y, z].
struct Position {
    std::optional<Vector3>& value;
};
bool ReadValue(const toml::Value& value, Position field) {
    Vector3 read;
    if (!dbtoml::ReadValue(value, read)) return false;
    field.value = read;
    return true;
}

// [x, y] pairs.
struct Places {
    std::vector<Vector2>& values;
};
bool ReadValue(const toml::Value& value, Places field) {
    if (!value.IsArray()) return false;
    std::vector<Vector2> read;
    for (int i = 0; i < value.Size(); ++i) {
        float point[2] = {};
        if (value.At(i).Size() != 2 || !dbtoml::ReadValue(value.At(i), point)) return false;
        read.push_back({point[0], point[1]});
    }
    field.values = std::move(read);
    return true;
}

// A zone's size in sectors, [x, y].
struct Size {
    int& x;
    int& y;
};
bool ReadValue(const toml::Value& value, Size field) {
    std::int32_t read[2] = {};
    if (value.Size() != 2 || !dbtoml::ReadValue(value, read)) return false;
    field.x = read[0];
    field.y = read[1];
    return true;
}

// "#restrict": [x, y, radius], the camera kept in that circle.
struct Restrict {
    MapRecord& map;
};
bool ReadValue(const toml::Value& value, Restrict field) {
    float x = 0.0f, y = 0.0f, radius = 0.0f;
    if (value.Size() != 3 || !toml::Read(value.At(0), &x) || !toml::Read(value.At(1), &y) ||
        !toml::Read(value.At(2), &radius))
        return false;
    field.map.restricted = true;
    field.map.restrictX = x;
    field.map.restrictY = y;
    field.map.restrictRadius = radius;
    return true;
}

// An angle in degrees (map.txt's), held in radians.
struct Degrees {
    float& value;
};
bool ReadValue(const toml::Value& value, Degrees field) {
    float degrees = 0.0f;
    if (!toml::Read(value, &degrees)) return false;
    field.value = ei::kRadiansPerDegree * degrees;
    return true;
}
struct DeployAngle {
    ZoneExit& exit;
};
bool ReadValue(const toml::Value& value, DeployAngle field) {
    if (!ReadValue(value, Degrees{field.exit.deployAngle})) return false;
    field.exit.hasDeployAngle = 1;
    return true;
}

// An exit's area that leads out, [x1, y1, x2, y2].
struct Area {
    ZoneExit& exit;
};
bool ReadValue(const toml::Value& value, Area field) {
    float read[4] = {};
    if (value.Size() != 4 || !dbtoml::ReadValue(value, read)) return false;
    field.exit.x1 = read[0];
    field.exit.y1 = read[1];
    field.exit.x2 = read[2];
    field.exit.y2 = read[3];
    return true;
}
struct Deploy {
    float (&values)[4];
};
bool ReadValue(const toml::Value& value, Deploy field) {
    float read[4] = {};
    if (value.Size() != 4 || !dbtoml::ReadValue(value, read)) return false;
    std::copy(std::begin(read), std::end(read), field.values);
    return true;
}

// The entrance as map.txt numbers the exits, from 1.
struct Number {
    int& value;
};
bool ReadValue(const toml::Value& value, Number field) {
    std::int32_t read = 0;
    if (!toml::Read(value, &read)) return false;
    field.value = read - 1;
    return true;
}

constexpr dbtoml::Name kKinds[] = {{0, "game"}, {1, "edge"}, {2, "brief"}};
constexpr dbtoml::Name kWeathers[] = {{0, "none"}, {1, "rain"}, {2, "snow"}, {3, "tornado"}};
constexpr dbtoml::Name kSkies[] = {{0, "normal"}, {1, "cave"}};

} // namespace
} // namespace mapdb

template <class Visit>
void Fields(AllodRecord& r, Visit& v) {
    v("name", mapdb::Text{r.name});
    v("figures", mapdb::Figures{r.otherNames});
}

template <class Visit>
void Fields(MapRecord& r, Visit& v) {
    v("name", mapdb::Text{r.name});
    v("allod", mapdb::Text{r.allod});
    v("kind", dbtoml::Enum<int>{r.kind, mapdb::kKinds});
    v("map", mapdb::Text{r.map});
    v("mob", mapdb::Text{r.mob});
    v("camera", mapdb::Text{r.camera});
    v("size", mapdb::Size{r.sizeX, r.sizeY});
    v("minimap", mapdb::Text{r.picture});
    v("objectives_map", mapdb::Text{r.picture2});
    v("figure", mapdb::Text{r.figure});
    v("position", mapdb::Position{r.position});
    v("weather", dbtoml::Enum<int>{r.climate, mapdb::kWeathers});
    v("sky", dbtoml::Enum<int>{r.cave, mapdb::kSkies});
    v("cage", dbtoml::Bool<bool>{r.cage});
    v("restrict", mapdb::Restrict{r});
}

template <class Visit>
void Fields(ZoneExit& r, Visit& v) {
    v("target", mapdb::Text{r.target});
    v("entrance", mapdb::Number{r.entrance});
    v("deploy", mapdb::Deploy{r.deploy});
    v("remove", mapdb::Area{r});
    v("angle", mapdb::Degrees{r.angle});
    v("pass_time", r.passTime);
    v("view", mapdb::Degrees{r.view});
    v("deploy_angle", mapdb::DeployAngle{r});
}

template <class Visit>
void Fields(MapQuestRecord& r, Visit& v) {
    v("name", mapdb::Text{r.name});
    v("position", mapdb::Position{r.position});
    v("places", mapdb::Places{r.places});
}

namespace mapdb {
namespace {

// The keys of the table `item` but `skip` read into `record`'s fields; the message of the first that does not load
// (`*line` its line), or empty.
template <class Record>
std::string ReadFields(const toml::Value& item, Record& record, std::string_view skip, int* line) {
    for (int k = 0; k < item.Size(); ++k) {
        const std::string_view key = item.KeyAt(k);
        if (key == skip) continue;
        *line = item.ValueAt(k).Line();
        dbtoml::FieldReader reader(key, item.ValueAt(k));
        Fields(record, reader);
        if (!reader.found) return "unknown key " + std::string(key);
        if (!reader.read) return reader.Refused();
    }
    return {};
}

// The index of the record named `name` (no case, as the Find functions), or the list's size.
template <class Record>
std::size_t IndexOf(const std::vector<Record>& records, std::string_view name) {
    std::size_t index = 0;
    while (index < records.size() && text::CompareIgnoringCase(records[index].name, name) != 0) ++index;
    return index;
}

// A zone's exits merged by number: `number` n (from 1) is the zone's n-th exit, which takes the fields given; an
// exit without one, or past the zone's last, is a new exit at the end (leading nowhere unless it has a target).
// `deploy` sets the area that leads out too unless `remove` is given, as map.txt's "#deploy" line does.
std::string MergeExits(const toml::Value& table, std::vector<ZoneExit>& exits, int* line) {
    if (!table.Exists()) return {};
    if (!table.IsArray()) {
        *line = table.Line();
        return "exit is not an array of tables";
    }
    for (int i = 0; i < table.Size(); ++i) {
        const toml::Value item = table.At(i);
        std::int32_t number = 0;
        if (item["number"].Exists() && !toml::Read(item["number"], &number)) {
            *line = item["number"].Line();
            return "number has a value it does not take";
        }
        const bool added = number < 1 || number > std::ssize(exits);
        ZoneExit exit;
        exit.target = "none";
        if (!added) exit = exits[number - 1];
        if (std::string refused = ReadFields(item, exit, "number", line); !refused.empty()) return refused;
        if (item["deploy"].Exists() && !item["remove"].Exists()) {
            exit.x1 = exit.deploy[0];
            exit.y1 = exit.deploy[1];
            exit.x2 = exit.deploy[2];
            exit.y2 = exit.deploy[3];
        }
        if (added) exits.push_back(exit);
        else exits[number - 1] = exit;
    }
    return {};
}

} // namespace
} // namespace mapdb

// Each record of a table by name: one the lists have (no case) takes the fields given and keeps the others; a new
// name is a new record at the end (a zone as map.txt's "#zone" line starts one: rain, its allod's map list). A record
// with a key it does not have, or a value its field does not take, is logged and left out whole.
void MapDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    using namespace mapdb;
    // `skip`: a key that `finish(item, record, index, line)` reads after the fields (its message, or empty).
    auto merge = [&path](const toml::Value& table, auto& records, std::string_view skip, auto finish) {
        if (table.Exists() && !table.IsArray()) {
            game::Warning("%s: %s line %d: not an array of tables", kWhat, path, table.Line());
            return;
        }
        for (int i = 0; i < table.Size(); ++i) {
            const toml::Value item = table.At(i);
            std::string name;
            if (!toml::Read(item["name"], &name)) {
                game::Warning("%s: %s line %d: a record without its name", kWhat, path, item.Line());
                continue;
            }
            const std::size_t index = IndexOf(records, name);
            const bool added = index == records.size();
            auto record = added ? typename std::remove_reference_t<decltype(records)>::value_type{} : records[index];
            int line = item.Line();
            std::string refused = ReadFields(item, record, skip, &line);
            if (refused.empty()) refused = finish(item, record, index, &line);
            if (!refused.empty()) {
                game::Warning("%s: %s line %d: %s; the record is left out", kWhat, path, line, refused);
                continue;
            }
            if (added) records.push_back(std::move(record));
            else records[index] = std::move(record);
        }
    };
    auto none = [](const toml::Value&, auto&, std::size_t, int*) { return std::string(); };
    merge(root["allod"], allods, "", none);
    merge(root["zone"], maps, "exit", [this](const toml::Value& item, MapRecord& zone, std::size_t index, int* line) {
        if (std::string refused = MergeExits(item["exit"], zone.exits, line); !refused.empty()) return refused;
        // The allods' map lists: a new zone joins its allod's, a zone given another allod moves.
        const int at = static_cast<int>(index);
        const bool added = index == maps.size();
        if (!added && text::CompareIgnoringCase(maps[index].allod, zone.allod) == 0) return std::string();
        if (AllodRecord* old = added ? nullptr : FindAllod(maps[index].allod)) std::erase(old->maps, at);
        if (AllodRecord* allod = FindAllod(zone.allod)) allod->maps.push_back(at);
        return std::string();
    });
    merge(root["quest"], quests, "", none);
    dbtoml::CheckTables(root, {"allod", "zone", "quest"}, kWhat, path);
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
