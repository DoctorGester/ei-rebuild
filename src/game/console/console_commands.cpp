// Console command handlers. Each gets the command's arguments as typed (keywords compare without case).
// See docs/notes/console.md.
#include "game/units/acks_db.h"
#include "game/app/app.h"
#include "game/console/console.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/core/text_split.h"
#include "game/ui/ui.h"
#include "game/ui/ui_camp.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "engine/core/named_floats.h"

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <format>

namespace console::commands {

namespace {



// Argument `i` equals `text`, without case.
bool ArgIs(const CommandArgs& args, int i, const char* text) {
    return text::CompareIgnoringCase(args[i], text) == 0;
}
// A log category's name for the filter command and its label in the list of shown ones.
struct LogName {
    const char* name = nullptr;
    const char* label = nullptr;
    LogCategory category = {};
};
constexpr LogName kLogNames[] = {
    {"ai", "AI: ", LogCategory::Ai},
    {"event", "\nEVENT: ", LogCategory::Event},
    {"graphics", "\nGRAPHICS: ", LogCategory::Graphics},
    {"rpg", "\nRPG: ", LogCategory::Rpg},
    {"unitacks", "\nUNITACKS: ", LogCategory::UnitAcks},
    {"net", "\nNET: ", LogCategory::Net},
};

// "1"/"on" and "0"/"off".
bool ArgOn(const CommandArgs& args, int i) { return ArgIs(args, i, "1") || ArgIs(args, i, "on"); }
bool ArgOff(const CommandArgs& args, int i) { return ArgIs(args, i, "0") || ArgIs(args, i, "off"); }

Player* LocalPlayer() {
    PlayerClient* view = g_clientWorld.LocalView();
    return view ? view->player : nullptr;
}


// The day number of a date (days since 1970-01-01, any year; a day past the month's end runs into the next month).
std::int64_t DaysFromCivil(int year, int month, int day) {
    const int y = month <= 2 ? year - 1 : year;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yearOfEra = y - era * 400;
    const int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5;
    const int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<std::int64_t>(era) * 146097 + dayOfEra - 719468 + (day - 1);
}

// A console position: its name, its letter and the window's rectangle (800x600 coordinates).
struct ConsolePosition {
    const char* name = nullptr;
    const char* letter = nullptr;
    Rect rect;
};
constexpr ConsolePosition kPositions[] = {
    {"left", "l", {0, 0, 200, 600}},
    {"right", "r", {600, 0, 800, 600}},
    {"top", "t", {0, 0, 800, 150}},
    {"bottom", "b", {0, 450, 800, 600}},
    {"center", "c", {200, 150, 600, 450}},
    {"fullscreen", "f", {0, 0, 800, 600}},
};

// The player list window's modes (Window::PlayerList).
constexpr int kKickList = 1;
constexpr int kBanList = 2;

// Days: the day number of a ddmmyyyy date, none unless it is eight digits (year 1900 or later, month 1-12, day 1-31).
std::optional<std::int64_t> DateDays(const std::string& text) {
    if (text.size() < 8) return std::nullopt;
    for (int i = 0; i < 8; ++i)
        if (text[i] < '0' || text[i] > '9') return std::nullopt;
    auto number = [&](int first, int count) { return str::ToInt(str::Mid(text, first, count)); };
    const int year = number(4, 4);
    const int month = number(2, 2);
    const int day = number(0, 2);
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return std::nullopt;
    return DaysFromCivil(year, month, day);
}

// join / server: a party with one member ("F", "Human Hero", `extra`) on the stack, handed
// to the game flow's request 5 (join) / 6 (host) with the map name.
void RequestWithParty(const CommandArgs& args, const char* extra, bool host) {
    Party party;
    party.AddMember("F", "Human Hero", extra);
    std::string map = args[0];
    if (host) g_gameFlow.RequestPartyZone6(map, &party);
    else g_gameFlow.RequestPartyZone5(map, &party);
}

void WriteSpeech(platform::File* file, const SpeechRecord& speech) {
    if (speech.text.size() == 0) return;
    file->WriteLine(std::format("#say {}", speech.name.data()));
    file->WriteLine(speech.text.data());
}

// kick / ban: without arguments the server's player list window (if the server allows it), with a
// name (quotes stripped) the server command.
void KickOrBan(const CommandArgs& args, int windowMode, net::CommandKind command) {
    int count = static_cast<int>(args.size());
    if (count == 0) {
        net::ClientSession* server = net::g_gameClient;
        if (IsLive(server)) {
            // Our binding's player record: only the host gets the window.
            net::BindingMessage* binding = server->binding;
            if (binding->player.Get()->host)
                g_interface.GetActiveWindow()->PlayerList(windowMode);
            return;
        }
    }
    if (count != 1) return;
    std::string name = args[0];
    if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
    if (name.empty() || name == "\"") return;   // no name: nothing to send
    if (IsLive(net::g_gameClient)) net::g_gameClient->SendCommand(command, name);
}

// rate / localrate: bytes per second, clamped to 500..100000.
void SetRate(const CommandArgs& args, int* rate, std::string_view usage) {
    if (args.size() != 1) {
        PrintLine(usage);
        return;
    }
    int value = str::ToInt(args[0]);
    if (value < 500) value = 500;
    else if (value > 100000) value = 100000;
    *rate = value;
}

} // namespace

void Help(const CommandArgs&) {
    static constexpr const char* kLines[] = {
        "Commands are:",
        "HELP",
        "  show this screen",
        "CONSOLE [pos]",
        "  change console position; <pos> = l<eft>, r<ight>, t<op>, b<ottom>, c<enter>, f<ullscreen>",
        "HISTORY [N lines]",
        "  change console history depth",
        "FILTER <[type] [1|on|0|off]]>",
        "  filter system messages; [type] = none, ai, event, graphics, rpg, net, unitacks, all",
        "LISTVAR [N player] <[output file name]>",
        "  dump list of global script variables to console and to file if needed",
        "LOADVAR [N player] [input file name]",
        "  load previously dumped list of global script variables from file",
        "LASTFPS",
        "  show last fps",
        "EXEC<UTE> [input file name]",
        "  execute text file with console commands",
        "QUIT, EXIT",
        "  exit game",
    };
    for (const char* line : kLines) PrintLine(line);
}

// Console l<eft>|r<ight>|t<op>|b<ottom>|c<enter>|f<ullscreen> (800x600 coordinates).
void ConsolePos(const CommandArgs& args) {
    if (!args.empty()) {
        for (const ConsolePosition& position : kPositions) {
            if (!ArgIs(args, 0, position.name) && !ArgIs(args, 0, position.letter)) continue;
            Rect rect = position.rect;
            g_interface.SetConsoleRect(rect);
            return;
        }
    }
    PrintLine("Invalid console position");
}

// History <lines>.
void History(const CommandArgs& args) {
    if (args.empty()) {
        PrintLine("Invalid parameters");
        return;
    }
    g_interface.SetConsoleHistory(str::ToInt(args[0]));
}

// Filter (prints the shown categories), filter <type> (shows only that one), filter <type> 1|on|0|off.
void Filter(const CommandArgs& args) {
    if (args.empty()) {
        Stream& os = g_consoleOut;
        for (const LogName& log : kLogNames)   // "AI: on", "\nEVENT: off", ...
            os << log.label << (lib::Has(g_filterMask, log.category) ? "on" : "off");
        EndLine(os);
        return;
    }
    LogCategory type = {};
    bool known = ArgIs(args, 0, "none");
    if (ArgIs(args, 0, "all")) {
        type = LogCategory::All;
        known = true;
    }
    for (const LogName& log : kLogNames) {
        if (!ArgIs(args, 0, log.name)) continue;
        type = log.category;
        known = true;
    }
    if (!known) {
        PrintLine("Invalid parameters");
        return;
    }
    if (args.size() <= 1) {
        g_filterMask = type;
    } else if (ArgOn(args, 1)) {
        g_filterMask |= type;
    } else if (ArgOff(args, 1)) {
        g_filterMask &= ~type;
    } else {
        PrintLine("Invalid parameters");
    }
}

// Listvar <player> [file]: prints the player's global variables ("name = value") and
// writes them to the file ("%s %g") when one is given. In a network game the local player is used.
void ListVar(const CommandArgs& args) {
    if (args.empty()) {
        PrintLine("usage: listvar [Nplayer] <[output file name]>");
        return;
    }
    Player* player;
    if (IsMultiplayerClient())
        player = LocalPlayer();
    else
        player = g_serverWorld.GetPlayer(str::ToInt(args[0]));
    if (!player) {
        PrintLine("Wrong player number");
        return;
    }

    platform::File file;
    bool writing = false;
    if (args.size() > 1) {
        writing = file.Open(args[1], platform::FileMode::Create);
        if (!writing)   // "Cannot open file <name> for writing"
            EndLine(g_consoleOut << "Cannot open file " << args[1] << " for writing");
    }
    const NamedFloatTable& variables = player->GetParty()->variables;
    for (auto& node : variables) {
        const std::string& name = node.first;
        EndLine(g_consoleOut << name << " = " << node.second);
        if (writing) file.WriteLine(str::Format("%s %g", name, static_cast<double>(node.second)));
    }
    if (writing) file.Close();
}

// Loadvar <player> <file>: sets the variables of each "name value" line.
void LoadVar(const CommandArgs& args) {
    if (args.size() < 2) {
        PrintLine("usage: loadvar [N player] [input file name]");
        return;
    }
    Player* player = g_serverWorld.GetPlayer(str::ToInt(args[0]));
    if (!player) {
        PrintLine("Wrong player number");
        return;
    }
    PlayerServer* state = player->Server();
    if (!state) {
        PrintLine("Wrong player number");
        return;
    }

    std::vector<std::string> parts;
    std::string bytes;
    if (!platform::ReadWholeFile(args[1], &bytes)) {   // "Cannot open file <name> for reading"
        EndLine(g_consoleOut << "Cannot open file " << args[1] << " for reading");
        return;
    }
    for (const std::string& line : str::Lines(bytes)) {
        parts = text::Split(line, ' ');
        if (parts.size() > 1) {
            const std::string& name = parts[0];
            float value = str::ToFloat(parts[1]);
            state->SetVariable(name, value);
        }
    }
}

// "Current FPS: " 1000 / last frame time (printed as float).
void LastFps(const CommandArgs&) {
    auto* timer = &g_sceneRenderer;
    Stream& os = g_consoleOut << "Current FPS: ";
    unsigned ms = timer->FrameTime();
    const float fps = 1000.0f / static_cast<float>(ms);   // a frame's milliseconds: exact in a float
    EndLine(os << fps);
}

// Exec <file>: runs each line of the file as a console command. Quotes around the name are trimmed.
void Exec(const CommandArgs& args) {
    if (args.empty()) {
        PrintLine("usage: exec<ute> [input file name]");
        return;
    }
    std::string name = args[0];
    str::TrimLeft(name, '"');
    str::TrimRight(name, '"');
    std::string bytes;
    if (!platform::ReadWholeFile(name, &bytes)) {   // "Cannot open file <name> for reading"
        EndLine(g_consoleOut << "Cannot open file " << name << " for reading");
        return;
    }
    for (const std::string& line : str::Lines(bytes)) console::Execute(line);
}

// Give <player> money <n> | exp <n> (shared by the party) | assortment.
void Give(const CommandArgs& args) {
    if (args.size() < 2) {
        PrintLine("IMPORTANT: use only from global map");
        PrintLine("give [NPlayer] money [NMoney]");
        PrintLine("give [NPlayer] exp [NExp]");
        return;
    }
    Player* player = g_serverWorld.GetPlayer(str::ToInt(args[0]));
    if (!player) {
        PrintLine("Wrong player number");
        return;
    }
    PlayerServer* state = player->Server();
    if (!state) {
        PrintLine("Wrong player number");
        return;
    }
    if (ArgIs(args, 1, "money") && args.size() > 2) {
        int amount = str::ToInt(args[2]);
        player->GetParty()->money += amount;
        state->SendParty();
        return;
    }
    if (ArgIs(args, 1, "exp") && args.size() > 2) {
        int count = lib::Count(player->GetParty()->members);
        if (count == 0) return;
        // The typed number over the count.
        const float share = str::ToFloat(args[2]) / static_cast<float>(count);
        for (int i = 0; i < count; ++i)
            player->GetParty()->members[i].stats.AddScaledExperience(share);
        state->SendParty();
        return;
    }
    if (ArgIs(args, 1, "assortment")) {
        if (CampPages* camp = g_interface.OnTop(g_interface.camp)) camp->DebugFill();
        return;
    }
    PrintLine("Give what?");
}

// join <map>: the flow's request 5 with a party of one ("Hercules").
void Join(const CommandArgs& args) {
    if (args.size() != 1) {
        PrintLine("Join has 1 parameter, server name");
        return;
    }
    RequestWithParty(args, "Hercules", false);
}

// server <map>: "Starting up a server", then the flow's request 6 with a party of one ("Serv").
void HostServer(const CommandArgs& args) {
    if (args.size() != 1) {
        PrintLine("Join has 1 parameter, map name");
        return;
    }
    PrintLine("Starting up a server");
    RequestWithParty(args, "Serv", true);
}

void MemUsage(const CommandArgs&) {}

// disconnect, any arguments: the client world's local player disconnects.
void Disconnect(const CommandArgs&) { g_clientWorld.DisconnectLocalPlayer(); }

void LootAll(const CommandArgs&) {
    PrintLine("Looting...");
    g_serverWorld.LootDeadUnits();
}

// Fadeout <milliseconds>.
void FadeOut(const CommandArgs& args) {
    if (args.empty()) {
        PrintLine("FADEOUT nMillSec");
        return;
    }
    if (!g_interface.fade) return;
    g_interface.fade->Start(str::ToInt(args[0]));
    PrintLine("Ok");
}

// Net client|server|time 0/1 (traffic and timing output), net zone <name>.
void Net(const CommandArgs& args) {
    if (args.size() != 2) {
        PrintLine("net client 0/1 - show client traffic");
        PrintLine("net server 0/1 - show server traffic");
        PrintLine("net time 0/1   - show client time counting info");
        return;
    }
    if (text::CompareIgnoringCase(args[0], "client") == 0) {
        if (Player* player = LocalPlayer()) player->logTraffic = str::ToInt(args[1]);
    }
    if (text::CompareIgnoringCase(args[0], "server") == 0) {
        if (Player* player = g_serverWorld.GetPlayer(0)) player->logTraffic = str::ToInt(args[1]);
    }
    if (text::CompareIgnoringCase(args[0], "time") == 0)
        app::g_showClientSpeed = str::ToInt(args[1]);
    if (text::CompareIgnoringCase(args[0], "zone") == 0 && IsLive(net::g_gameClient)) {
        std::string zone = args[1];
        net::g_gameClient->SendCommand(net::CommandKind::PreviewMap, zone);
    }
}

// Every speech of the list that has a text, as two lines: "#say <name>" and
// the text.

void WriteSpeechLines(platform::File* file, const acks::Speeches* list) {
    if (!list) return;
    for (const SpeechRecord& speech : list->records) WriteSpeech(file, speech);
}

void WriteEnvironmentSpeechLines(platform::File* file, const acks::EnvironmentSpeeches* list) {
    if (!list) return;
    for (const acks::EnvironmentSpeech& entry : list->entries) WriteSpeech(file, entry.speech);
}

// Generateacks. Writes <data folder>scenack.txt: for every record of the three ack
// tables, each speech list its kind map names (in the map's order; a list that serves several
// kinds is written once per kind), and after a unit record's lists its environment speeches.
void GenerateAcks(const CommandArgs&) {
    platform::File file;
    if (!file.Open(g_dataFolder + "scenack.txt", platform::FileMode::Create)) return;
    platform::File* out = &file;
    acks::AckDatabase& database = g_ackDatabase;
    for (acks::UnitAcks& record : database.units.records) {
        for (const auto& [kind, speeches] : record.head.speeches) WriteSpeechLines(out, speeches);
        WriteEnvironmentSpeechLines(out, &record.environmentSpeeches);
    }
    for (acks::ModelAcks& record : database.models.records)
        for (const auto& [kind, speeches] : record.head.speeches) WriteSpeechLines(out, speeches);
    for (acks::ExtraAcks& record : database.extras.records)
        for (const auto& [kind, speeches] : record.head.speeches) WriteSpeechLines(out, speeches);
    file.Close();
}

// Days ddmmyyyy DDMMYYYY: the days between two dates (06:00:00 and 07:01:01), counted on the calendar.
void Days(const CommandArgs& args) {
    if (args.size() >= 2) {
        const std::optional<std::int64_t> from = DateDays(args[0]);
        const std::optional<std::int64_t> to = DateDays(args[1]);
        if (from && to) {
            // 06:00:00 on the first date to 07:01:01 on the second, whole days.
            int days = static_cast<int>(((*to - *from) * 86400 + 3661) / 86400);
            EndLine(g_consoleOut << "Your age: " << days << " days");
            return;
        }
    }
    static constexpr const char* kUsage[] = {
        "usage: days [ddmmyyyy] [DDMMYYYY]",
        "ddmmyyyy - date of birth",
        "DDMMYYYY - current date",
        "example:",
        "> days 25011978 01072000",
        "Your age: 8193 days",
    };
    for (const char* line : kUsage) PrintLine(line);
}

void PartyResend(const CommandArgs&) {
    if (Player* player = g_serverWorld.GetPlayer(0)) player->Server()->ResendParty();
}

// Kick [name].
void Kick(const CommandArgs& args) { KickOrBan(args, kKickList, net::CommandKind::KickByName); }

// Ban [name].
void Ban(const CommandArgs& args) { KickOrBan(args, kBanList, net::CommandKind::BanByName); }

// Fps [1|on|0|off]: shows or hides the frame rate; anything else toggles it.
void Fps(const CommandArgs& args) {
    bool on = g_interface.fpsSurface == nullptr;
    if (!args.empty() && ArgOn(args, 0)) on = true;
    else if (!args.empty() && ArgOff(args, 0)) on = false;
    g_interface.ShowFps(on);
}

// Rate <bytes per second>.
void Rate(const CommandArgs& args) { SetRate(args, &net::g_remoteRate, "Rate has one parameter - connection rate in bytes per second"); }

// Localrate <bytes per second>.
void LocalRate(const CommandArgs& args) { SetRate(args, &net::g_localRate, "LocalRate has one parameter - connection rate in bytes per second"); }

} // namespace console::commands
