// Console command handlers. Each gets the command's arguments, already lower-cased.
// See docs/notes/console.md.
#include "app.h"
#include "ei/x87.h"
#include "console.h"

#include "../lib/crt.h"
#include "player_client.h"
#include "text_split.h"

#include "lib/crt.h"
#include "net_classes.h"
#include "acks_db.h"
#include "world_glue.h"
#include "model.h"
#include "net_messages.h"
#include "party.h"
#include "player_messages.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace console::commands {

namespace {


int Mbscmp(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}
int Mbsicmp(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b);
}

// Argument `i` equals `text` (_mbscmp).
bool ArgIs(const Arguments* args, int i, const char* text) { return Mbscmp((*args)[i].c_str(), text) == 0; }
// "1"/"on" and "0"/"off".
bool ArgOn(const Arguments* args, int i) { return ArgIs(args, i, "1") || ArgIs(args, i, "on"); }
bool ArgOff(const Arguments* args, int i) { return ArgIs(args, i, "0") || ArgIs(args, i, "off"); }

CPlayer* GetPlayer(int id) { return TheServerWorld()->GetPlayer(id); }
CPlayer* LocalPlayer() {
    return TheClientWorld()->LocalView()->player_;
}

inline net::ClientSession* Server() { return net::GameClient(); }
bool ServerActive(net::ClientSession* server) { return server && !(server->flags7_ & CRefCounted::kDetached); }

void SetDebugInfoRect(int left, int top, int right, int bottom) {
    RECT rect = {left, top, right, bottom};
    TheInterface()->SetDebugInfoRect(&rect);
}
void SetConsoleRect(int left, int top, int right, int bottom) {
    RECT rect = {left, top, right, bottom};
    TheInterface()->SetConsoleRect(&rect);
}

} // namespace

void Help(Arguments*) {
    static constexpr const char* kLines[] = {
        "Commands are:",
        "HELP",
        "  show this screen",
        "DEBUGINFO [pos]",
        "  change debug info position; <pos> = l<eft>, r<ight>, rt, righttop, lt, lefttop",
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

// Debuginfo l<eft>|r<ight>|lt|lefttop|rt|righttop.
void DebugInfo(Arguments* args) {
    if (!args->empty()) {
        if (ArgIs(args, 0, "left") || ArgIs(args, 0, "l"))
            return SetDebugInfoRect(10, 10, 0x122, 0x24E);
        if (ArgIs(args, 0, "right") || ArgIs(args, 0, "r"))
            return SetDebugInfoRect(0x1FE, 10, 0x316, 0x24E);
        if (ArgIs(args, 0, "lefttop") || ArgIs(args, 0, "lt"))
            return SetDebugInfoRect(10, 10, 0x122, 0x122);
        if (ArgIs(args, 0, "righttop") || ArgIs(args, 0, "rt"))
            return SetDebugInfoRect(0x1FE, 10, 0x316, 0x122);
    }
    PrintLine("Invalid debug info position");
}

// Console l<eft>|r<ight>|t<op>|b<ottom>|c<enter>|f<ullscreen> (800x600 coordinates).
void ConsolePos(Arguments* args) {
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
void History(Arguments* args) {
    if (args->empty()) return PrintLine("Invalid parameters");
    TheInterface()->SetConsoleHistory(std::atoi((*args)[0].c_str()));
}

// Filter (prints the shown categories), filter <type> (shows only that one), filter <type> 1|on|0|off.
void Filter(Arguments* args) {
    if (args->empty()) {
        static constexpr const char* kLabels[] = {"AI: ", "\nEVENT: ", "\nGRAPHICS: ", "\nRPG: ", "\nUNITACKS: ", "\nNET: "};
        GameOStream& os = Out();
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

// Show fps|managers|render_info|world_data 1|on|0|off (anything else: nothing).
void Show(Arguments* args) {
    if (args->size() < 2) return PrintLine("Invalid number of parameters for command \"show\"");
    void (CWorldClient::*on)();
    void (CWorldClient::*off)();
    if (ArgIs(args, 0, "fps")) {
        on = &CWorldClient::ShowFps;
        off = &CWorldClient::HideFps;
    } else if (ArgIs(args, 0, "managers")) {
        on = &CWorldClient::ShowManagers;
        off = &CWorldClient::HideManagers;
    } else if (ArgIs(args, 0, "render_info")) {
        on = &CWorldClient::ShowRenderInfo;
        off = &CWorldClient::HideRenderInfo;
    } else if (ArgIs(args, 0, "world_data")) {
        on = &CWorldClient::ShowWorldData;
        off = &CWorldClient::HideWorldData;
    } else {
        return PrintLine("Invalid parameters in command 'show'");
    }
    if (ArgOn(args, 1)) (TheClientWorld()->*on)();
    else if (ArgOff(args, 1)) (TheClientWorld()->*off)();
}

// Listvar <player> [file]: prints the player's global variables ("name = value") and
// writes them to the file ("%s %g") when one is given. In a network game the local player is used.
void ListVar(Arguments* args) {
    if (args->empty()) return PrintLine("usage: listvar [Nplayer] <[output file name]>");
    CPlayer* player;
    if (IsMultiplayerClient())
        player = LocalPlayer();
    else
        player = GetPlayer(std::atoi((*args)[0].c_str()));
    if (!player) return PrintLine("Wrong player number");

    std::string line;
    CStdioFile file;
    mfc::BOOL writing = 0;
    if (args->size() > 1) {
        writing = file.Open((*args)[1], CStdioFile::modeCreate | CStdioFile::modeWrite, nullptr);
        if (!writing)   // "Cannot open file <name> for writing"
            EndLine(Out() << "Cannot open file " << (*args)[1].c_str() << " for writing");
    }
    const NamedFloatTable& variables = player->Party()->variables_;
    for (const NamedFloatTable::Node* node = variables.First(); node; node = variables.Next(node)) {
        const char* name = node->value.key;
        EndLine((Out() << name << " = ").PutFloat(node->value.mapped));
        if (writing) {
            line = str::Format("%s %g\n", name, static_cast<double>(node->value.mapped));
            file.WriteString(line.c_str());
        }
    }
    if (writing) file.Close();
}

// Loadvar <player> <file>: sets the variables of each "name value" line.
void LoadVar(Arguments* args) {
    if (args->size() < 2) return PrintLine("usage: loadvar [N player] [input file name]");
    CPlayer* player = GetPlayer(std::atoi((*args)[0].c_str()));
    if (!player) return PrintLine("Wrong player number");
    CPlayerServer* state = player->Server();
    if (!state) return PrintLine("Wrong player number");

    mfc::CString line;
    std::vector<std::string> parts;
    CStdioFile file;
    if (!file.Open((*args)[1], CStdioFile::modeRead, nullptr)) {   // "Cannot open file <name> for reading"
        EndLine(Out() << "Cannot open file " << (*args)[1].c_str() << " for reading");
        return;
    }
    while (file.ReadString(line)) {
        text::Split(line, &parts, ' ');
        if (parts.size() > 1) {
            const char* name = parts[0].c_str();
            float value = static_cast<float>(std::atof(parts[1].c_str()));
            state->SetVariable(name, std::bit_cast<std::uint32_t>(value));
        }
    }
    file.Close();
}

// "Current FPS: " 1000 / last frame time (printed as float).
void LastFps(Arguments*) {
    auto* timer = SceneRenderer();
    GameOStream& os = Out() << "Current FPS: ";
    unsigned ms = timer->FrameTime();
    double fps = 1000.0f /*0073C34C*/ / x87::FromUnsigned(ms);
    EndLine(os.PutFloat(fps));
}

// Exec <file>: runs each line of the file as a console command. Quotes around the name
// are trimmed (the message names the argument as given).
void Exec(Arguments* args) {
    mfc::CString line;
    if (args->empty()) return PrintLine("usage: exec<ute> [input file name]");
    std::string name = (*args)[0];
    str::TrimLeft(name, '"');
    str::TrimRight(name, '"');
    CStdioFile file;
    if (!file.Open(name, CStdioFile::typeText, nullptr)) {   // "Cannot open file <name> for reading"
        EndLine(Out() << "Cannot open file " << (*args)[0].c_str() << " for reading");
        return;
    }
    while (file.ReadString(line)) console::Execute(line);
    file.Close();
}

// Give <player> money <n> | exp <n> (shared by the party) | assortment.
void Give(Arguments* args) {
    std::string unused1;
    std::string unused2;
    if (args->size() < 2) {
        PrintLine("IMPORTANT: use only from global map");
        PrintLine("give [NPlayer] money [NMoney]");
        return PrintLine("give [NPlayer] exp [NExp]");
    }
    CPlayer* player = GetPlayer(std::atoi((*args)[0].c_str()));
    if (!player) return PrintLine("Wrong player number");
    CPlayerServer* state = player->Server();
    if (!state) return PrintLine("Wrong player number");
    if (ArgIs(args, 1, "money") && args->size() > 2) {
        int amount = std::atoi((*args)[2].c_str());
        player->Party()->Money() += amount;
        return state->SendParty();
    }
    if (ArgIs(args, 1, "exp") && args->size() > 2) {
        int count = player->Party()->members_.size;
        // Rounded to float once.
        float share = static_cast<float>(std::atof((*args)[2].c_str()) / count);
        for (int i = 0; i < count; ++i)
            player->Party()->members_.data[i].stats.AddScaledExperience(share);
        return state->SendParty();
    }
    if (ArgIs(args, 1, "assortment")) {
        ScreenOf(TheInterface()->GetActiveWindow())->DebugFill();
        return;
    }
    PrintLine("Give what?");
}

namespace {
// join / server: a party with one member ("F", "Human Hero", `extra`) on the stack, handed
// to the game flow's request 5 / 6 with the map name.
template <void (GameFlow::*Request)(const char*, CParty*)>
void RequestWithParty(Arguments* args, const char* extra) {
    CParty party;
    party.AddMember("F", "Human Hero", extra);
    std::string map = (*args)[0];
    (TheGameFlow()->*Request)(map.c_str(), &party);
}
} // namespace

// join <map>: the flow's request 5 with a party of one ("Hercules").
void Join(Arguments* args) {
    if (args->size() != 1) return PrintLine("Join has 1 parameter, server name");
    RequestWithParty<&GameFlow::RequestPartyZone5>(args, "Hercules");
}

// server <map>: "Starting up a server", then the flow's request 6 with a party of one ("Serv").
void HostServer(Arguments* args) {
    if (args->size() != 1) return PrintLine("Join has 1 parameter, map name");
    PrintLine("Starting up a server");
    RequestWithParty<&GameFlow::RequestPartyZone6>(args, "Serv");
}

void MemUsage(Arguments*) {}

// disconnect, any arguments: the client world's local player disconnects.
void Disconnect(Arguments*) { TheGameFlow()->DisconnectClient(); }

void LootAll(Arguments*) {
    PrintLine("Looting...");
    TheServerWorld()->LootDeadUnits();
}

// Fadeout <milliseconds>.
void FadeOut(Arguments* args) {
    if (args->empty()) return PrintLine("FADEOUT nMillSec");
    TheInterface()->fade_->Start(std::atoi((*args)[0].c_str()));
    PrintLine("Ok");
}

// Net client|server|time 0/1 (traffic and timing output), net zone <name>.
void Net(Arguments* args) {
    if (args->size() != 2) {
        PrintLine("net client 0/1 - show client traffic");
        PrintLine("net server 0/1 - show server traffic");
        return PrintLine("net time 0/1   - show client time counting info");
    }
    if (Mbsicmp((*args)[0].c_str(), "client") == 0) {
        std::string value = (*args)[1];
        LocalPlayer()->logTraffic_ = std::atoi(value.c_str());
    }
    if (Mbsicmp((*args)[0].c_str(), "server") == 0) {
        if (CPlayer* player = GetPlayer(0)) player->logTraffic_ = std::atoi((*args)[1].c_str());
    }
    if (Mbsicmp((*args)[0].c_str(), "time") == 0)
        app::g_showClientSpeed = std::atoi((*args)[1].c_str());
    if (Mbsicmp((*args)[0].c_str(), "zone") == 0 && ServerActive(Server())) {
        std::string zone = (*args)[1];
        Server()->SendCommand(0, zone.c_str());
    }
}

// Every speech of the list that has a text, as two lines: "#say <name>" and
// the text. The lines go through the file's WriteString slot.
namespace {
void WriteSpeech(CStdioFile* file, mfc::CString& line, const SpeechRecord& speech) {
    if (speech.text.size() == 0) return;
    line.Format("#say %s\n", speech.name.data());
    file->WriteString(line);
    line.Format("%s\n", speech.text.data());
    file->WriteString(line);
}
} // namespace

void WriteSpeechLines(CStdioFile* file, const acks::SpeechList* list) {
    if (!list) return;
    mfc::CString line;
    for (const SpeechRecord& speech : *list) WriteSpeech(file, line, speech);
}

void WriteEnvironmentSpeechLines(CStdioFile* file, const acks::EnvironmentSpeeches* list) {
    if (!list) return;
    mfc::CString line;
    for (const acks::EnvironmentSpeech& entry : *list) WriteSpeech(file, line, entry.speech);
}

// Generateacks. Writes <data folder>scenack.txt: for every record of the three ack
// tables, each speech list its kind map names (in the map's order; a list that serves several
// kinds is written once per kind), and after a unit record's lists its environment speeches.
void GenerateAcks(Arguments*) {
    CStdioFile file;
    {
        std::string path = (*GameDataPathString() + "scenack.txt").GetString();
        if (!file.Open(path, CStdioFile::modeCreate | CStdioFile::modeWrite, nullptr)) return;
    }
    CStdioFile* out = &file;
    acks::AckDatabase& database = acks::Database();
    for (acks::UnitAcks& record : database.units.records) {
        record.head.speeches.ForEach([out](const auto* node) { WriteSpeechLines(out, node->value); });
        WriteEnvironmentSpeechLines(out, &record.environmentSpeeches);
    }
    for (acks::ModelAcks& record : database.models.records)
        record.head.speeches.ForEach([out](const auto* node) { WriteSpeechLines(out, node->value); });
    for (acks::ExtraAcks& record : database.extras.records)
        record.head.speeches.ForEach([out](const auto* node) { WriteSpeechLines(out, node->value); });
    file.Close();
}

// Days ddmmyyyy DDMMYYYY: the days between two dates (06:00:00 and 07:01:01 local time).
void Days(Arguments* args) {
    auto digits = [&](int i) {
        for (int k = 0; k < 8; ++k) {
            signed char c = (*args)[i].c_str()[k];
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
                    mfc::CTime birth;
                    long from = birth.Construct(year, month, day, 6, 0, 0, -1)->time_;
                    int year2 = number(1, 4, 4);
                    if (year2 >= 1900) {
                        int month2 = number(1, 2, 2);
                        if (month2 >= 1 && month2 <= 12) {
                            int day2 = number(1, 0, 2);
                            if (day2 >= 1 && day2 <= 31) {
                                mfc::CTime now;
                                long to = now.Construct(year2, month2, day2, 7, 1, 1, -1)->time_;
                                int days = static_cast<int>((to - from) / 86400);
                                EndLine(Out() << "Your age: " << days << " days");
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

void PartyResend(Arguments*) {
    if (CPlayer* player = GetPlayer(0)) player->Server()->ResendParty();
}

namespace {
// kick / ban: without arguments the server's player list window (if the server allows it), with a
// name (quotes stripped) the server command.
void KickOrBan(Arguments* args, int windowMode, int command) {
    int count = static_cast<int>(args->size());
    if (count == 0) {
        net::ClientSession* server = Server();
        if (ServerActive(server)) {
            // Our binding's player record: only the host gets the window.
            net::BindingMessage* binding = server->binding_;
            if (binding->Player()->host_)
                ScreenOf(TheInterface()->GetActiveWindow())->PlayerList(windowMode);
            return;
        }
    }
    if (count != 1 || ArgIs(args, 0, "")) return;   // "" does nothing
    std::string name = (*args)[0];
    const char* text = name.c_str();
    if (text[0] == '"' && text[static_cast<int>(name.size()) - 1] == '"') name = str::Mid(name, 1, static_cast<int>(name.size()) - 2);
    if (ServerActive(Server())) Server()->SendCommand(command, name.c_str());
}
} // namespace

// Kick [name].
void Kick(Arguments* args) { KickOrBan(args, 1, 2); }

// Ban [name].
void Ban(Arguments* args) { KickOrBan(args, 2, 3); }

// Fps 1|on|0|off (anything else: nothing).
void Fps(Arguments* args) {
    if (args->size() < 1) return PrintLine("Invalid number of parameters for command \"FPS\"");
    if (ArgOn(args, 0)) TheInterface()->ShowFps(1);
    else if (ArgOff(args, 0)) TheInterface()->ShowFps(0);
}

namespace {
// rate / localrate: bytes per second, clamped to 500..100000.
void SetRate(Arguments* args, int* rate, const char* usage) {
    if (args->size() != 1) return PrintLine(usage);
    int value = std::atoi((*args)[0].c_str());
    if (value < 500) value = 500;
    else if (value > 100000) value = 100000;
    *rate = value;
}
} // namespace

// Rate <bytes per second>.
void Rate(Arguments* args) { SetRate(args, &net::g_remoteRate, "Rate has one parameter - connection rate in bytes per second"); }

// Localrate <bytes per second>.
void LocalRate(Arguments* args) { SetRate(args, &net::g_localRate, "LocalRate has one parameter - connection rate in bytes per second"); }

} // namespace console::commands
