// Console command handlers. Each gets the command's arguments, already lower-cased.
// See docs/notes/console.md.
#include "acks_db.h"
#include "app.h"
#include "console.h"
#include "game_singletons.h"
#include "model.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "player_client.h"
#include "player_messages.h"
#include "text_split.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "world_glue.h"

#include "lib/calendar.h"
#include "lib/file_io.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/x87.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace console::commands {

namespace {



// Argument `i` equals `text` (_mbscmp).
bool ArgIs(const std::vector<std::string>* args, int i, const char* text) { return str::Compare((*args)[i], text) == 0; }
// "1"/"on" and "0"/"off".
bool ArgOn(const std::vector<std::string>* args, int i) { return ArgIs(args, i, "1") || ArgIs(args, i, "on"); }
bool ArgOff(const std::vector<std::string>* args, int i) { return ArgIs(args, i, "0") || ArgIs(args, i, "off"); }

CPlayer* GetPlayer(int id) { return g_serverWorld->GetPlayer(id); }
CPlayer* LocalPlayer() {
    return g_clientWorld->LocalView()->player_;
}

bool ServerActive(net::ClientSession* server) { return server && !(server->flags7_ & CRefCounted::kDetached); }

void SetConsoleRect(int left, int top, int right, int bottom) {
    RECT rect = {left, top, right, bottom};
    g_interface.SetConsoleRect(&rect);
}

// join / server: a party with one member ("F", "Human Hero", `extra`) on the stack, handed
// to the game flow's request 5 / 6 with the map name.
template <void (GameFlow::*Request)(const char*, CParty*)>
void RequestWithParty(std::vector<std::string>* args, const char* extra) {
    CParty party;
    party.AddMember("F", "Human Hero", extra);
    std::string map = (*args)[0];
    (g_gameFlow.Get().*Request)(map.c_str(), &party);
}

void WriteSpeech(lib::TextFile* file, std::string& line, const SpeechRecord& speech) {
    if (speech.text.size() == 0) return;
    line = str::Format("#say %s\n", speech.name.data());
    file->WriteString(line);
    line = str::Format("%s\n", speech.text.data());
    file->WriteString(line);
}

// kick / ban: without arguments the server's player list window (if the server allows it), with a
// name (quotes stripped) the server command.
void KickOrBan(std::vector<std::string>* args, int windowMode, int command) {
    int count = static_cast<int>(args->size());
    if (count == 0) {
        net::ClientSession* server = net::g_gameClient;
        if (ServerActive(server)) {
            // Our binding's player record: only the host gets the window.
            net::BindingMessage* binding = server->binding_;
            if (binding->Player()->host_)
                ScreenOf(g_interface.GetActiveWindow())->PlayerList(windowMode);
            return;
        }
    }
    if (count != 1 || ArgIs(args, 0, "")) return;   // "" does nothing
    std::string name = (*args)[0];
    if (name[0] == '"' && name[name.size() - 1] == '"') name = str::Mid(name, 1, static_cast<int>(name.size()) - 2);
    if (ServerActive(net::g_gameClient)) net::g_gameClient->SendCommand(command, name);
}

// rate / localrate: bytes per second, clamped to 500..100000.
void SetRate(std::vector<std::string>* args, int* rate, const char* usage) {
    if (args->size() != 1) return PrintLine(usage);
    int value = str::ToInt((*args)[0]);
    if (value < 500) value = 500;
    else if (value > 100000) value = 100000;
    *rate = value;
}

} // namespace

void Help(std::vector<std::string>*) {
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
void ConsolePos(std::vector<std::string>* args) {
    if (!args->empty()) {
        if (ArgIs(args, 0, "left") || ArgIs(args, 0, "l"))
            return SetConsoleRect(0, 0, 0xC8, 0x258);
        if (ArgIs(args, 0, "right") || ArgIs(args, 0, "r"))
            return SetConsoleRect(0x258, 0, 0x320, 0x258);
        if (ArgIs(args, 0, "top") || ArgIs(args, 0, "t"))
            return SetConsoleRect(0, 0, 0x320, 0x96);
        if (ArgIs(args, 0, "bottom") || ArgIs(args, 0, "b"))
            return SetConsoleRect(0, 0x1C2, 0x320, 0x258);
        if (ArgIs(args, 0, "center") || ArgIs(args, 0, "c"))
            return SetConsoleRect(0xC8, 0x96, 0x258, 0x1C2);
        if (ArgIs(args, 0, "fullscreen") || ArgIs(args, 0, "f"))
            return SetConsoleRect(0, 0, 0x320, 0x258);
    }
    PrintLine("Invalid console position");
}

// History <lines>.
void History(std::vector<std::string>* args) {
    if (args->empty()) return PrintLine("Invalid parameters");
    g_interface.SetConsoleHistory(str::ToInt((*args)[0]));
}

// Filter (prints the shown categories), filter <type> (shows only that one), filter <type> 1|on|0|off.
void Filter(std::vector<std::string>* args) {
    if (args->empty()) {
        static constexpr const char* kLabels[] = {"AI: ", "\nEVENT: ", "\nGRAPHICS: ", "\nRPG: ", "\nUNITACKS: ", "\nNET: "};
        Stream& os = g_consoleOut;
        for (int i = 0; i < 6; ++i)   // "AI: on", "\nEVENT: off", ...
            os << kLabels[i] << (g_filterMask & (1u << i) ? "on" : "off");
        return EndLine(os);
    }
    unsigned type;
    if (ArgIs(args, 0, "none")) type = 0;
    else if (ArgIs(args, 0, "ai")) type = 1;
    else if (ArgIs(args, 0, "event")) type = 2;
    else if (ArgIs(args, 0, "graphics")) type = 4;
    else if (ArgIs(args, 0, "rpg")) type = 8;
    else if (ArgIs(args, 0, "unitacks")) type = 0x10;
    else if (ArgIs(args, 0, "net")) type = 0x20;
    else if (ArgIs(args, 0, "all")) type = ~0u;
    else return PrintLine("Invalid parameters");
    if (args->size() <= 1) {
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
void ListVar(std::vector<std::string>* args) {
    if (args->empty()) return PrintLine("usage: listvar [Nplayer] <[output file name]>");
    CPlayer* player;
    if (IsMultiplayerClient())
        player = LocalPlayer();
    else
        player = GetPlayer(str::ToInt((*args)[0]));
    if (!player) return PrintLine("Wrong player number");

    std::string line;
    lib::TextFile file;
    bool writing = false;
    if (args->size() > 1) {
        writing = file.Open((*args)[1], "w");
        if (!writing)   // "Cannot open file <name> for writing"
            EndLine(g_consoleOut << "Cannot open file " << (*args)[1] << " for writing");
    }
    const NamedFloatTable& variables = player->Party()->variables_;
    for (auto& node : variables) {
        const std::string& name = node.first;
        EndLine(g_consoleOut << name << " = " << node.second);
        if (writing) {
            line = str::Format("%s %g\n", name, static_cast<double>(node.second));
            file.WriteString(line);
        }
    }
    if (writing) file.Close();
}

// Loadvar <player> <file>: sets the variables of each "name value" line.
void LoadVar(std::vector<std::string>* args) {
    if (args->size() < 2) return PrintLine("usage: loadvar [N player] [input file name]");
    CPlayer* player = GetPlayer(str::ToInt((*args)[0]));
    if (!player) return PrintLine("Wrong player number");
    CPlayerServer* state = player->Server();
    if (!state) return PrintLine("Wrong player number");

    std::string line;
    std::vector<std::string> parts;
    lib::TextFile file;
    if (!file.Open((*args)[1], "r")) {   // "Cannot open file <name> for reading"
        EndLine(g_consoleOut << "Cannot open file " << (*args)[1] << " for reading");
        return;
    }
    while (file.ReadLine(line)) {
        text::Split(line, &parts, ' ');
        if (parts.size() > 1) {
            const std::string& name = parts[0];
            float value = static_cast<float>(str::ToDouble(parts[1]));
            state->SetVariable(name, std::bit_cast<std::uint32_t>(value));
        }
    }
    file.Close();
}

// "Current FPS: " 1000 / last frame time (printed as float).
void LastFps(std::vector<std::string>*) {
    auto* timer = &g_sceneRenderer.Get();
    Stream& os = g_consoleOut << "Current FPS: ";
    unsigned ms = timer->FrameTime();
    double fps = 1000.0f / x87::FromUnsigned(ms);
    EndLine(os << fps);
}

// Exec <file>: runs each line of the file as a console command. Quotes around the name
// are trimmed (the message names the argument as given).
void Exec(std::vector<std::string>* args) {
    std::string line;
    if (args->empty()) return PrintLine("usage: exec<ute> [input file name]");
    std::string name = (*args)[0];
    str::TrimLeft(name, '"');
    str::TrimRight(name, '"');
    lib::TextFile file;
    if (!file.Open(name, "r")) {   // "Cannot open file <name> for reading"
        EndLine(g_consoleOut << "Cannot open file " << (*args)[0] << " for reading");
        return;
    }
    while (file.ReadLine(line)) console::Execute(line);
    file.Close();
}

// Give <player> money <n> | exp <n> (shared by the party) | assortment.
void Give(std::vector<std::string>* args) {
    std::string unused1;
    std::string unused2;
    if (args->size() < 2) {
        PrintLine("IMPORTANT: use only from global map");
        PrintLine("give [NPlayer] money [NMoney]");
        return PrintLine("give [NPlayer] exp [NExp]");
    }
    CPlayer* player = GetPlayer(str::ToInt((*args)[0]));
    if (!player) return PrintLine("Wrong player number");
    CPlayerServer* state = player->Server();
    if (!state) return PrintLine("Wrong player number");
    if (ArgIs(args, 1, "money") && args->size() > 2) {
        int amount = str::ToInt((*args)[2]);
        player->Party()->money_ += amount;
        return state->SendParty();
    }
    if (ArgIs(args, 1, "exp") && args->size() > 2) {
        int count = std::ssize(player->Party()->members_);
        // Rounded to float once.
        float share = static_cast<float>(str::ToDouble((*args)[2]) / count);
        for (int i = 0; i < count; ++i)
            player->Party()->members_[i].stats.AddScaledExperience(share);
        return state->SendParty();
    }
    if (ArgIs(args, 1, "assortment")) {
        ScreenOf(g_interface.GetActiveWindow())->DebugFill();
        return;
    }
    PrintLine("Give what?");
}

// join <map>: the flow's request 5 with a party of one ("Hercules").
void Join(std::vector<std::string>* args) {
    if (args->size() != 1) return PrintLine("Join has 1 parameter, server name");
    RequestWithParty<&GameFlow::RequestPartyZone5>(args, "Hercules");
}

// server <map>: "Starting up a server", then the flow's request 6 with a party of one ("Serv").
void HostServer(std::vector<std::string>* args) {
    if (args->size() != 1) return PrintLine("Join has 1 parameter, map name");
    PrintLine("Starting up a server");
    RequestWithParty<&GameFlow::RequestPartyZone6>(args, "Serv");
}

void MemUsage(std::vector<std::string>*) {}

// disconnect, any arguments: the client world's local player disconnects.
void Disconnect(std::vector<std::string>*) { g_gameFlow->DisconnectClient(); }

void LootAll(std::vector<std::string>*) {
    PrintLine("Looting...");
    g_serverWorld->LootDeadUnits();
}

// Fadeout <milliseconds>.
void FadeOut(std::vector<std::string>* args) {
    if (args->empty()) return PrintLine("FADEOUT nMillSec");
    g_interface.fade_->Start(str::ToInt((*args)[0]));
    PrintLine("Ok");
}

// Net client|server|time 0/1 (traffic and timing output), net zone <name>.
void Net(std::vector<std::string>* args) {
    if (args->size() != 2) {
        PrintLine("net client 0/1 - show client traffic");
        PrintLine("net server 0/1 - show server traffic");
        return PrintLine("net time 0/1   - show client time counting info");
    }
    if (text::CompareIgnoringCase((*args)[0], "client") == 0) {
        std::string value = (*args)[1];
        LocalPlayer()->logTraffic_ = str::ToInt(value);
    }
    if (text::CompareIgnoringCase((*args)[0], "server") == 0) {
        if (CPlayer* player = GetPlayer(0)) player->logTraffic_ = str::ToInt((*args)[1]);
    }
    if (text::CompareIgnoringCase((*args)[0], "time") == 0)
        app::g_showClientSpeed = str::ToInt((*args)[1]);
    if (text::CompareIgnoringCase((*args)[0], "zone") == 0 && ServerActive(net::g_gameClient)) {
        std::string zone = (*args)[1];
        net::g_gameClient->SendCommand(0, zone);
    }
}

// Every speech of the list that has a text, as two lines: "#say <name>" and
// the text. The lines go through the file's WriteString slot.

void WriteSpeechLines(lib::TextFile* file, const acks::Speeches* list) {
    if (!list) return;
    std::string line;
    for (const SpeechRecord& speech : *list) WriteSpeech(file, line, speech);
}

void WriteEnvironmentSpeechLines(lib::TextFile* file, const acks::EnvironmentSpeeches* list) {
    if (!list) return;
    std::string line;
    for (const acks::EnvironmentSpeech& entry : *list) WriteSpeech(file, line, entry.speech);
}

// Generateacks. Writes <data folder>scenack.txt: for every record of the three ack
// tables, each speech list its kind map names (in the map's order; a list that serves several
// kinds is written once per kind), and after a unit record's lists its environment speeches.
void GenerateAcks(std::vector<std::string>*) {
    lib::TextFile file;
    {
        std::string path = g_dataFolder + "scenack.txt";
        if (!file.Open(path, "w")) return;
    }
    lib::TextFile* out = &file;
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

// Days ddmmyyyy DDMMYYYY: the days between two dates (06:00:00 and 07:01:01 local time).
void Days(std::vector<std::string>* args) {
    auto digits = [&](int i) {
        for (int k = 0; k < 8; ++k) {
            signed char c = (*args)[i][k];
            if (c < '0' || c > '9') return false;
        }
        return true;
    };
    auto number = [&](int i, int first, int count) { return std::atoi(str::Mid((*args)[i], first, count).c_str()); };
    if (args->size() >= 2 && (*args)[0].size() >= 8 && (*args)[1].size() >= 8 && digits(0) && digits(1)) {
        int year = number(0, 4, 4);
        if (year >= 1900) {
            int month = number(0, 2, 2);
            if (month >= 1 && month <= 12) {
                int day = number(0, 0, 2);
                if (day >= 1 && day <= 31) {
                    const std::int32_t from = lib::MakeTime(year, month, day, 6, 0, 0, -1);
                    int year2 = number(1, 4, 4);
                    if (year2 >= 1900) {
                        int month2 = number(1, 2, 2);
                        if (month2 >= 1 && month2 <= 12) {
                            int day2 = number(1, 0, 2);
                            if (day2 >= 1 && day2 <= 31) {
                                const std::int32_t to = lib::MakeTime(year2, month2, day2, 7, 1, 1, -1);
                                int days = static_cast<int>((to - from) / 86400);
                                EndLine(g_consoleOut << "Your age: " << days << " days");
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
    static constexpr const char* kUsage[] = {
        "usage: days [ddmmyyyy] [DDMMYYYY]",
        "ddmmyyyy - date of birth",
        "DDMMYYYY - current date",
        "example:",
        "> days 25011978 01072000",
        "Your age: 8192 days",
    };
    for (const char* line : kUsage) PrintLine(line);
}

void PartyResend(std::vector<std::string>*) {
    if (CPlayer* player = GetPlayer(0)) player->Server()->ResendParty();
}

// Kick [name].
void Kick(std::vector<std::string>* args) { KickOrBan(args, 1, 2); }

// Ban [name].
void Ban(std::vector<std::string>* args) { KickOrBan(args, 2, 3); }

// Fps 1|on|0|off (anything else: nothing).
void Fps(std::vector<std::string>* args) {
    if (args->size() < 1) return PrintLine("Invalid number of parameters for command \"FPS\"");
    if (ArgOn(args, 0)) g_interface.ShowFps(1);
    else if (ArgOff(args, 0)) g_interface.ShowFps(0);
}

// Rate <bytes per second>.
void Rate(std::vector<std::string>* args) { SetRate(args, &net::g_remoteRate, "Rate has one parameter - connection rate in bytes per second"); }

// Localrate <bytes per second>.
void LocalRate(std::vector<std::string>* args) { SetRate(args, &net::g_localRate, "LocalRate has one parameter - connection rate in bytes per second"); }

} // namespace console::commands
