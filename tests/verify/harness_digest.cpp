// The game-state digest of harness runs (harness_digest.h): the records, their writing and the steps. The state is
// read by harness_digest_read.inc, over game.exe's objects in its runs (harness_digest_exe.cpp) and over ours in the
// image's (image/harness_digest_image.cpp).
#include "harness_digest.h"
#include "harness_digest_read.h"

#include "harness_clock.h"
#include "harness_heap.h"
#include "harness_original.h"
#include "harness_starter.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cctype>
#include <cstdlib>

namespace ei::verify::digest {

namespace {

bool SameName(const Field& a, const Field& b) {
    return a.index == b.index && (a.name == b.name || std::strcmp(a.name, b.name) == 0);
}

constexpr const char* kGroupNames[kGroups] = {"world", "random", "player", "obj", "timed",
                                              "missile", "aievents", "script", "client", "cunit", "water"};
// The groups beyond digest 1's, as the title line lists them (and "water" after "digest water").
constexpr const char* kNewGroups = "timed,missile,aievents,script,client,cunit";
bool g_water = false;
std::string NewGroups() { return std::string(kNewGroups) + (g_water ? ",water" : ""); }

// Groups of one entity are named alone ("world"); the others take the id ("obj12").
bool Single(int group) { return group == kWorld || group == kRandom || group == kEvents || group == kScript || group == kClient; }

std::string KeyName(const Key& key) {
    std::string name = kGroupNames[key.first];
    return Single(key.first) ? name : name + std::to_string(key.second);
}

// The AI modes' kinds. A mode's id in the digest is game.exe's vtable address for its kind (what the goldens hold):
// game.exe's modes keep that table; the image's are C++ classes, named by their type (ModeKindId).
struct ModeKind {
    const void* original;   // game.exe's table: the id
    const char* name;
};

const std::vector<ModeKind>& ModeKinds() {
    static const std::vector<ModeKind> kinds = {
        {&ei::OriginalGameData<const std::uint8_t>(0x0073D128), "guard"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073FF40), "patrol"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073D190), "sentry"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073D1F8), "suspection"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073D260), "aggression"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073D294), "corpsewatcher"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073F0F8), "player"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073FF8C), "fear"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073F0C4), "tamed"},
        {&ei::OriginalGameData<const std::uint8_t>(0x0073D1C4), "follow"},
    };
    return kinds;
}

// The name of the kind an id belongs to, or null for one not listed.
const char* ModeName(const void* id) {
    for (const ModeKind& kind : ModeKinds())
        if (kind.original == id) return kind.name;
    return nullptr;
}

// The map nodes of the record before the last, reused for the next one: a record has some 1,500
// entities, and making and freeing their nodes and field vectors was about half the digest's samples
// (SV10; reusing them takes ~20% off its time). A reused node keeps its vector's room; Read inserts in
// about key order, so a key mostly gets the node it had.
State g_spare;

// The state of the server world `server` (game.exe's objects in its runs, ours in the image's), and its hashes.
void Read(void* server, Snapshot& s) {
    if (ei::Standalone()) image_side::Read(server, s);
    else exe_side::Read(server, s);
    Hash all, section[kGroups];
    for (const auto& [key, entity] : s.state) {
        Hash& h = section[key.first];
        h.Add(static_cast<std::uint32_t>(key.second));
        for (const Field& f : entity.fields) h.Add(f.value);
        h.Add(static_cast<std::uint32_t>(entity.fields.size()));
    }
    for (int g = 0; g < kGroups; ++g) {
        s.sections[g] = section[g].h;
        if (g != kWater || g_water) all.Add(s.sections[g]);   // without the water group: the hashes as before
    }
    all.AddText(s.zone.c_str());
    s.all = all.h;
}

} // namespace

const char* Intern(const std::string& name) {
    static std::set<std::string> names;
    return names.insert(name).first->c_str();
}

Entity& Slot(State& state, const Key& key) {
    if (g_spare.empty()) return state[key];
    State::node_type node = g_spare.extract(g_spare.begin());
    node.key() = key;
    node.mapped().description.clear();
    node.mapped().fields.clear();
    auto placed = state.insert(std::move(node));
    return placed.position->second;   // an entity of that key already there: it, as state[key] gives
}

bool WaterOn() { return g_water; }

std::uint32_t ModeKindId(std::size_t index) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ModeKinds()[index].original));
}

namespace {

// --- Writing ---

std::string FieldName(const Field& f) {
    std::string name = f.name;
    if (f.index >= 0) {
        std::size_t at = name.find('#');
        if (at != std::string::npos) name.replace(at, 1, std::to_string(f.index));
    }
    return name;
}

std::string FieldValue(const Field& f) {
    char text[40];
    switch (f.kind) {
    case Kind::Int: std::snprintf(text, sizeof(text), "%d", static_cast<int>(f.value)); break;
    case Kind::Unsigned: std::snprintf(text, sizeof(text), "%u", f.value); break;
    case Kind::Bits: std::snprintf(text, sizeof(text), "0x%08X", f.value); break;
    case Kind::Mode: {
        // An AI mode by name, one ModeName does not know by its id (its table; none: 0).
        const char* mode = ModeName(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(f.value)));
        if (mode) std::snprintf(text, sizeof(text), "%s", mode);
        else std::snprintf(text, sizeof(text), "0x%08X", f.value);
        break;
    }
    case Kind::Float: {
        float v;
        std::memcpy(&v, &f.value, 4);
        // A bit test (rewrite guide rule 7: /fp:fast folds the comparisons).
        if ((f.value & 0x7F800000u) == 0x7F800000u && (f.value & 0x007FFFFFu)) {
            std::snprintf(text, sizeof(text), "nan:0x%08X", f.value);   // which NaN, too
        } else {
            std::snprintf(text, sizeof(text), "%.9g", v);
            if (!std::strpbrk(text, ".eni")) strcat_s(text, ".0");   // a float always shows it is one
        }
        break;
    }
    }
    return text;
}

void WriteField(FILE* f, const std::string& key, const Field& field) {
    std::fprintf(f, "%s %s %s\n", key.c_str(), FieldName(field).c_str(), FieldValue(field).c_str());
}

void WriteHeader(FILE* f, unsigned record, unsigned update, unsigned frame, int tick, const Snapshot& s) {
    const std::uint32_t* h = s.sections;
    std::fprintf(f, "@ %u update %u frame %u tick %d zone %s hash %08X world %08X random %08X players %08X objects %08X "
                    "timed %08X missiles %08X events %08X script %08X client %08X n %u\n",
                 record, update, frame, tick, s.zone.empty() ? "-" : s.zone.c_str(), s.all, h[kWorld], h[kRandom], h[kPlayer],
                 h[kObject], h[kTimed], h[kMissile], h[kEvents], h[kScript], h[kClient] ^ h[kClientUnit],
                 unsigned(s.state.size()));
}

// What changed from `before` to `now`, in record order.
void WriteDelta(FILE* f, const State& before, const State& now) {
    for (const auto& [key, entity] : now) {
        const std::string name = KeyName(key);
        auto old = before.find(key);
        if (old == before.end() || old->second.description != entity.description) {
            if (old != before.end()) std::fprintf(f, "- %s\n", name.c_str());
            std::fprintf(f, "+ %s %s\n", name.c_str(), entity.description.c_str());
            for (const Field& field : entity.fields) WriteField(f, name, field);
            continue;
        }
        const std::vector<Field>& a = old->second.fields;
        const std::vector<Field>& c = entity.fields;
        bool sameLayout = a.size() == c.size();
        for (std::size_t i = 0; sameLayout && i < a.size(); ++i) sameLayout = SameName(a[i], c[i]);
        if (sameLayout) {
            for (std::size_t i = 0; i < c.size(); ++i)
                if (a[i].value != c[i].value || a[i].kind != c[i].kind) WriteField(f, name, c[i]);
            continue;
        }
        for (const Field& field : c) {
            auto it = std::find_if(a.begin(), a.end(), [&](const Field& x) { return SameName(x, field); });
            if (it == a.end() || it->value != field.value || it->kind != field.kind) WriteField(f, name, field);
        }
        for (const Field& field : a)
            if (std::none_of(c.begin(), c.end(), [&](const Field& x) { return SameName(x, field); }))
                std::fprintf(f, "%s %s -\n", name.c_str(), FieldName(field).c_str());
    }
    for (const auto& [key, entity] : before)
        if (!now.count(key)) std::fprintf(f, "- %s\n", KeyName(key).c_str());
}

// --- The digest's state ---

const unsigned* g_frames = nullptr;
unsigned g_period = 0;          // world updates between records (0: none)
unsigned g_updates = 0;         // world updates seen
unsigned g_nextAt = 0;          // the update count of the next periodic record
unsigned g_records = 0;
std::uint32_t g_lastHash = 0;
void* g_server = nullptr;       // the server world (game.exe's or ours)
FILE* g_file = nullptr;
State g_previous;

FILE* OpenOutput(const char* name, const char* mode) {
    char path[MAX_PATH];
    ei::OutputPath(name, path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, mode) != 0) return nullptr;
    return f;
}

bool OpenDigest() {
    if (g_file) return true;
    g_file = OpenOutput("digest.txt", "w");
    if (!g_file) return false;
    std::setvbuf(g_file, nullptr, _IOFBF, 1 << 20);
    char hooks[8] = "";
    GetEnvironmentVariableA("EI_NO_HOOKS", hooks, sizeof(hooks));
    std::fprintf(g_file, "# ei state digest 2; groups %s; hooks %s; install %s; every %u world updates\n", NewGroups().c_str(),
                 hooks[0] == '1' ? "off" : "on", starter::RebirthInstall() ? "rebirth" : "vanilla", g_period);
    return true;
}

void TakeRecord() {
    if (!g_server || !OpenDigest()) return;
    Snapshot s;
    Read(g_server, s);
    WriteHeader(g_file, g_records, g_updates, g_frames ? *g_frames : 0, s.tick, s);
    WriteDelta(g_file, g_previous, s.state);
    clock::RandomNote("digest %u update %u hash %08X", g_records, g_updates, s.all);
    ++g_records;
    g_lastHash = s.all;
    g_spare.clear();
    g_spare.swap(g_previous);
    g_previous = std::move(s.state);
}

} // namespace

void Init(const unsigned* frames) {
    g_frames = frames;
    char value[32] = "";
    if (GetEnvironmentVariableA("EI_DIGEST", value, sizeof(value))) {
        g_period = std::strtoul(value, nullptr, 10);
        g_nextAt = g_period;
        if (g_period) Log("harness: state digest every %u world updates (EI_DIGEST)", g_period);
    }
}

void AfterWorldUpdate(void* server) {
    g_server = server;
    ++g_updates;
    if (heap::TraceUntil() && g_updates == heap::TraceUntil()) {
        if (ei::Standalone()) image_side::NoteAddresses(g_server);   // the heap trace's end
        else exe_side::NoteAddresses(g_server);
        heap::StopTrace();
    }
    if (g_period && g_updates >= g_nextAt) {
        g_nextAt = g_updates + g_period;
        TakeRecord();
    }
}

void WithWater() { g_water = true; }

std::string Step(const std::string& argument) {
    if (argument.empty() || argument == "now") {
        if (!g_server) return "digest: no world update has run yet";
        TakeRecord();
        return "";
    }
    if (argument == "off") {
        g_period = 0;
        return "";
    }
    char* end = nullptr;
    unsigned long period = std::strtoul(argument.c_str(), &end, 10);
    if (!end || *end) return "digest: '" + argument + "' is not a period (world updates), off or now";
    g_period = period;
    g_nextAt = g_updates + period;
    Log("harness: state digest every %u world updates", g_period);
    return "";
}

std::string Dump(const std::string& name) {
    bool valid = !name.empty() && name.size() < 64;
    for (char c : name) valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.');
    if (!valid) return "dump: bad name '" + name + "'";
    if (!g_server) return "dump: no world update has run yet";
    FILE* f = OpenOutput(("dump_" + name + ".txt").c_str(), "w");
    if (!f) return "dump: cannot write dump_" + name + ".txt";
    Snapshot s;
    Read(g_server, s);
    std::fprintf(f, "# ei state digest 2; groups %s; dump %s\n", NewGroups().c_str(), name.c_str());
    WriteHeader(f, g_records, g_updates, g_frames ? *g_frames : 0, s.tick, s);
    WriteDelta(f, State{}, s.state);
    std::fclose(f);
    Log("harness: dump %s: hash %08X, %u entities", name.c_str(), s.all, unsigned(s.state.size()));
    return "";
}

UnitNow Unit(int id) {
    if (!g_server || id < 0) return {};
    return ei::Standalone() ? image_side::Unit(g_server, id) : exe_side::Unit(g_server, id);
}

int LocalLeader(unsigned* money) {
    if (!g_server) return -1;
    return ei::Standalone() ? image_side::LocalLeader(g_server, money) : exe_side::LocalLeader(g_server, money);
}

std::string Summary() {
    if (!g_records) return "";
    char text[120];
    std::snprintf(text, sizeof(text), "%u records in digest.txt, the last after world update %u: hash %08X", g_records,
                  g_updates, g_lastHash);
    return text;
}

void Flush() {
    if (g_file) std::fflush(g_file);
}

} // namespace ei::verify::digest
