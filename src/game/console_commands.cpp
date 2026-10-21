// Console command handlers (game.exe 0x005DD920-0x005E0A8B). Each gets the command's arguments,
// already lower-cased, in ECX. See docs/notes/console.md.
#include "ei/x87.h"
#include "console.h"

#include "../lib/crt.h"
#include "text_split.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "net_classes.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "acks_db.h"
#include "world_glue.h"
#include "originals_units.h"
#include "model.h"
#include "net_messages.h"
#include "party.h"
#include "player_messages.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console::commands {

namespace {


int Mbscmp(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
int Mbsicmp(const char* a, const char* b) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// Argument `i` equals `text` (_mbscmp).
bool ArgIs(const CStringArray* args, int i, const char* text) { return Mbscmp((*args)[i], text) == 0; }
// "1"/"on" and "0"/"off".
bool ArgOn(const CStringArray* args, int i) { return ArgIs(args, i, "1") || ArgIs(args, i, "on"); }
bool ArgOff(const CStringArray* args, int i) { return ArgIs(args, i, "0") || ArgIs(args, i, "off"); }

// 007C7D50: the interface manager.

// The world's player `id` (00594180, through its address), or null.
WorldPlayer* GetPlayer(int id) { return static_cast<WorldPlayer*>(static_cast<CWorldServer*>(WorldServer())->GetPlayer(id)); }
// The client world's local player (its local view's player).
WorldPlayer* LocalPlayer() {
    return reinterpret_cast<LocalViewData*>(static_cast<CWorldClient*>(WorldClient())->LocalView())->player;
}

// 0079B5D8: the network client session, active unless it was detached.
inline net::ClientSession* Server() { return net::GameClient(); }
bool ServerActive(net::ClientSession* server) { return server && !(server->flags7_ & CRefCounted::kDetached); }

void SetDebugInfoRect(int left, int top, int right, int bottom) {
    RECT rect = {left, top, right, bottom};
    TheInterface()->SetDebugInfoRect(&rect);
}
void SetConsoleRect(int left, int top, int right, int bottom) {
    int rect[4] = {left, top, right, bottom};
    TheInterface()->SetConsoleRect(rect);
}

} // namespace

// 005DF5E0
void __fastcall Help(CStringArray*) {
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
EI_REPLACE(0x005DF5E0, Help);

// 005DD920: debuginfo l<eft>|r<ight>|lt|lefttop|rt|righttop.
void __fastcall DebugInfo(CStringArray* args) {
    if (args->GetSize() != 0) {
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
EI_REPLACE(0x005DD920, DebugInfo);

// 005DDBE0: console l<eft>|r<ight>|t<op>|b<ottom>|c<enter>|f<ullscreen> (800x600 coordinates).
void __fastcall ConsolePos(CStringArray* args) {
    if (args->GetSize() != 0) {
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
EI_REPLACE(0x005DDBE0, ConsolePos);

// 005DDF80: history <lines>.
void __fastcall History(CStringArray* args) {
    if (args->GetSize() == 0) return PrintLine("Invalid parameters");
    TheInterface()->SetConsoleHistory(libcrt::Atoi((*args)[0]));
}
EI_REPLACE(0x005DDF80, History);

// 005DDFE0: filter (prints the shown categories), filter <type> (shows only that one),
// filter <type> 1|on|0|off.
void __fastcall Filter(CStringArray* args) {
    if (args->GetSize() == 0) {
        static constexpr const char* kLabels[] = {"AI: ", "\nEVENT: ", "\nGRAPHICS: ", "\nRPG: ", "\nUNITACKS: ", "\nNET: "};
        GameOStream& os = Out();
        for (int i = 0; i < 6; ++i)   // "AI: on", "\nEVENT: off", ...
            os << kLabels[i] << (FilterMask() & (1u << i) ? "on" : "off");
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
    if (args->GetSize() <= 1) {
        FilterMask() = type;
    } else if (ArgOn(args, 1)) {
        FilterMask() |= type;
    } else if (ArgOff(args, 1)) {
        FilterMask() &= ~type;
    } else {
        PrintLine("Invalid parameters");
    }
}
EI_REPLACE(0x005DDFE0, Filter);

// 005DE460: show fps|managers|render_info|world_data 1|on|0|off (anything else: nothing).
void __fastcall Show(CStringArray* args) {
    if (args->GetSize() < 2) return PrintLine("Invalid number of parameters for command \"show\"");
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
    if (ArgOn(args, 1)) (WorldClient()->*on)();
    else if (ArgOff(args, 1)) (WorldClient()->*off)();
}
EI_REPLACE(0x005DE460, Show);

// 005DEA00: listvar <player> [file]: prints the player's global variables ("name = value") and
// writes them to the file ("%s %g") when one is given. In a network game the local player is used.
void __fastcall ListVar(CStringArray* args) {
    if (args->GetSize() == 0) return PrintLine("usage: listvar [Nplayer] <[output file name]>");
    WorldPlayer* player;
    if (IsMultiplayerClient())
        player = LocalPlayer();
    else
        player = GetPlayer(libcrt::Atoi((*args)[0]));
    if (!player) return PrintLine("Wrong player number");

    mfc::CString line;
    CStdioFile file;
    mfc::BOOL writing = 0;
    if (args->GetSize() > 1) {
        writing = file.Open((*args)[1], CStdioFile::modeCreate | CStdioFile::modeWrite, nullptr);
        if (!writing)   // "Cannot open file <name> for writing"
            EndLine(Out() << "Cannot open file " << static_cast<const char*>((*args)[1]) << " for writing");
    }
    const NamedFloatTable& variables = player->variables;
    for (const NamedFloatTable::Node* node = variables.First(); node; node = variables.Next(node)) {
        const char* name = node->value.key;
        EndLine((Out() << name << " = ").PutFloat(node->value.mapped));
        if (writing) {
            line.Format("%s %g\n", name, static_cast<double>(node->value.mapped));
            file.WriteString(line);
        }
    }
    if (writing) file.Close();
}
EI_REPLACE(0x005DEA00, ListVar);

// 005DEC90: loadvar <player> <file>: sets the variables of each "name value" line.
void __fastcall LoadVar(CStringArray* args) {
    if (args->GetSize() < 2) return PrintLine("usage: loadvar [N player] [input file name]");
    WorldPlayer* player = GetPlayer(libcrt::Atoi((*args)[0]));
    if (!player) return PrintLine("Wrong player number");
    CPlayerServer* state = player->server;
    if (!state) return PrintLine("Wrong player number");

    mfc::CString line;
    CStringArray parts;
    CStdioFile file;
    if (!file.Open((*args)[1], CStdioFile::modeRead, nullptr)) {   // "Cannot open file <name> for reading"
        EndLine(Out() << "Cannot open file " << static_cast<const char*>((*args)[1]) << " for reading");
        return;
    }
    while (file.ReadString(line)) {
        text::Split(line, &parts, ' ');
        if (parts.GetSize() > 1) {
            const char* name = parts[0];
            float value = static_cast<float>(GameAtof(parts[1]));
            state->SetVariable(name, std::bit_cast<std::uint32_t>(value));
        }
    }
    file.Close();
}
EI_REPLACE(0x005DEC90, LoadVar);

// 005DEEE0: "Current FPS: " 1000 / last frame time (printed as float).
void __fastcall LastFps(CStringArray*) {
    auto* timer = static_cast<CSceneRenderer*>(SceneRenderer());
    GameOStream& os = Out() << "Current FPS: ";
    unsigned ms = timer->FrameTime();
    double fps = 1000.0f /*0073C34C*/ / x87::FromUnsigned(ms);
    EndLine(os.PutFloat(fps));
}
EI_REPLACE(0x005DEEE0, LastFps);

// 005DEF50: exec <file>: runs each line of the file as a console command. Quotes around the name
// are trimmed (the message names the argument as given).
void __fastcall Exec(CStringArray* args) {
    mfc::CString line;
    mfc::CString name;
    if (args->GetSize() == 0) return PrintLine("usage: exec<ute> [input file name]");
    name = (*args)[0];
    Ops(name).TrimLeft('"');
    Ops(name).TrimRight('"');
    CStdioFile file;
    if (!file.Open(name, CStdioFile::typeText, nullptr)) {   // "Cannot open file <name> for reading"
        EndLine(Out() << "Cannot open file " << static_cast<const char*>((*args)[0]) << " for reading");
        return;
    }
    while (file.ReadString(line)) ExecuteAt(line);
    file.Close();
}
EI_REPLACE(0x005DEF50, Exec);

// 005DF0F0: give <player> money <n> | exp <n> (shared by the party) | assortment.
void __fastcall Give(CStringArray* args) {
    mfc::CString unused1;
    mfc::CString unused2;
    if (args->GetSize() < 2) {
        PrintLine("IMPORTANT: use only from global map");
        PrintLine("give [NPlayer] money [NMoney]");
        return PrintLine("give [NPlayer] exp [NExp]");
    }
    WorldPlayer* player = GetPlayer(libcrt::Atoi((*args)[0]));
    if (!player) return PrintLine("Wrong player number");
    CPlayerServer* state = player->server;
    if (!state) return PrintLine("Wrong player number");
    if (ArgIs(args, 1, "money") && args->GetSize() > 2) {
        int amount = libcrt::Atoi((*args)[2]);
        player->Party()->Money() += amount;
        return state->SendParty();
    }
    if (ArgIs(args, 1, "exp") && args->GetSize() > 2) {
        int count = player->mercCount;
        // Rounded to float once, as the original stores it.
        float share = static_cast<float>(GameAtof((*args)[2]) / count);
        for (int i = 0; i < count; ++i)
            reinterpret_cast<UnitStats*>(reinterpret_cast<PartyUnitRecord*>(player->mercs)[i].stats)->AddScaledExperience(share);
        return state->SendParty();
    }
    if (ArgIs(args, 1, "assortment")) {
        InterfaceActiveWindowOriginal(TheInterface())->VirtualDebugFill();
        return;
    }
    PrintLine("Give what?");
}
EI_REPLACE(0x005DF0F0, Give);

namespace {
// join / server (S16b): a party with one member ("F", "Human Hero", `extra`) on the stack, handed
// to the game flow's request 5 / 6 (00581730 / 00581760: a copy of it) with the map name.
template <void (GameFlow::*Request)(const char*, const void*)>
void RequestWithParty(CStringArray* args, const char* extra) {
    alignas(CParty) unsigned char storage[sizeof(CParty)];
    CParty* party = reinterpret_cast<CParty*>(storage);
    party->Construct();
    party->AddMember("F", "Human Hero", extra);
    {
        mfc::CString map((*args)[0]);
        (TheGameFlow()->*Request)(map, party);
    }
    party->DestructBody();
}
} // namespace

// 005DF3A0 (no Ghidra function; S16b): join <map>: the flow's request 5 with a party of one
// ("Hercules").
void __fastcall Join(CStringArray* args) {
    if (args->GetSize() != 1) return PrintLine("Join has 1 parameter, server name");
    RequestWithParty<&GameFlow::RequestPartyZone5>(args, "Hercules");
}
EI_REPLACE(0x005DF3A0, Join);

// 005DF490 (no Ghidra function; S16b): server <map>: "Starting up a server", then the flow's
// request 6 with a party of one ("Serv").
void __fastcall HostServer(CStringArray* args) {
    if (args->GetSize() != 1) return PrintLine("Join has 1 parameter, map name");
    PrintLine("Starting up a server");
    RequestWithParty<&GameFlow::RequestPartyZone6>(args, "Serv");
}
EI_REPLACE(0x005DF490, HostServer);

// 005DF5A0 (a jump to the empty 00455CD0; S16b): memusage does nothing.
void __fastcall MemUsage(CStringArray*) {}
EI_REPLACE(0x005DF5A0, MemUsage);

// 005DFC10 (a jump to GameFlow 00581620; no Ghidra function, S16c): disconnect, any arguments: the
// client world's local player disconnects.
void __fastcall Disconnect(CStringArray*) { TheGameFlow()->DisconnectClient(); }
EI_REPLACE(0x005DFC10, Disconnect);

// 005DF5B0: lootall.
void __fastcall LootAll(CStringArray*) {
    PrintLine("Looting...");
    WorldServer()->LootDeadUnits();
}
EI_REPLACE(0x005DF5B0, LootAll);

// 005DF8E0: fadeout <milliseconds>.
void __fastcall FadeOut(CStringArray* args) {
    if (args->GetSize() <= 0) return PrintLine("FADEOUT nMillSec");
    TheInterface()->fade_->Start(libcrt::Atoi((*args)[0]));
    PrintLine("Ok");
}
EI_REPLACE(0x005DF8E0, FadeOut);

// 005DF9B0: net client|server|time 0/1 (traffic and timing output), net zone <name>.
void __fastcall Net(CStringArray* args) {
    if (args->GetSize() != 2) {
        PrintLine("net client 0/1 - show client traffic");
        PrintLine("net server 0/1 - show server traffic");
        return PrintLine("net time 0/1   - show client time counting info");
    }
    if (Mbsicmp((*args)[0], "client") == 0) {
        mfc::CString value((*args)[1]);
        LocalPlayer()->logTraffic = libcrt::Atoi(value);
    }
    if (Mbsicmp((*args)[0], "server") == 0) {
        if (WorldPlayer* player = GetPlayer(0)) player->logTraffic = libcrt::Atoi((*args)[1]);
    }
    if (Mbsicmp((*args)[0], "time") == 0)
        *ei::GamePointer<int>(0x0079BC60) = libcrt::Atoi((*args)[1]);
    if (Mbsicmp((*args)[0], "zone") == 0 && ServerActive(Server())) {
        mfc::CString zone((*args)[1]);
        Server()->SendCommand(0, zone);
    }
}
EI_REPLACE(0x005DF9B0, Net);

// 005DFC20 / 005DFCE0. Every speech of the list that has a text, as two lines: "#say <name>" and
// the text. The lines go through the file's WriteString slot.
namespace {
void WriteSpeech(CStdioFileVirtuals* file, mfc::CString& line, const SpeechRecord& speech) {
    if (speech.text.size() == 0) return;
    line.Format("#say %s\n", speech.name.data());
    file->WriteString(line);
    line.Format("%s\n", speech.text.data());
    file->WriteString(line);
}
} // namespace

void __fastcall WriteSpeechLines(CStdioFileVirtuals* file, const acks::SpeechList* list) {
    if (!list) return;
    mfc::CString line;
    for (const SpeechRecord& speech : *list) WriteSpeech(file, line, speech);
}
EI_REPLACE(0x005DFC20, WriteSpeechLines);

void __fastcall WriteEnvironmentSpeechLines(CStdioFileVirtuals* file,
                                            const stlp::vector<acks::EnvironmentSpeech>* list) {
    if (!list) return;
    mfc::CString line;
    for (const acks::EnvironmentSpeech& entry : *list) WriteSpeech(file, line, entry.speech);
}
EI_REPLACE(0x005DFCE0, WriteEnvironmentSpeechLines);

// 005DFDA0: generateacks. Writes <data folder>scenack.txt: for every record of the three ack
// tables, each speech list its kind map names (in the map's order; a list that serves several
// kinds is written once per kind), and after a unit record's lists its environment speeches.
void __fastcall GenerateAcks(CStringArray*) {
    CStdioFile file;
    {
        mfc::CString path = *GameDataPathString() + "scenack.txt";
        if (!file.Open(path, CStdioFile::modeCreate | CStdioFile::modeWrite, nullptr)) return;
    }
    CStdioFileVirtuals* out = file.Virtuals();
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
EI_REPLACE(0x005DFDA0, GenerateAcks);

// 005E00D0: days ddmmyyyy DDMMYYYY: the days between two dates (06:00:00 and 07:01:01 local time).
void __fastcall Days(CStringArray* args) {
    auto digits = [&](int i) {
        for (int k = 0; k < 8; ++k) {
            signed char c = static_cast<const char*>((*args)[i])[k];
            if (c < '0' || c > '9') return false;
        }
        return true;
    };
    auto number = [&](int i, int first, int count) { return libcrt::Atoi(Ops((*args)[i]).Mid(first, count)); };
    if (args->GetSize() >= 2 && Length((*args)[0]) >= 8 && Length((*args)[1]) >= 8 && digits(0) && digits(1)) {
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
EI_REPLACE(0x005E00D0, Days);

// 005E0570: partyresend.
void __fastcall PartyResend(CStringArray*) {
    if (WorldPlayer* player = GetPlayer(0)) static_cast<CPlayerServer*>(player->server)->ResendParty();
}
EI_REPLACE(0x005E0570, PartyResend);

namespace {
// kick / ban: without arguments the server's player list window (if the server allows it), with a
// name (quotes stripped) the server command.
void KickOrBan(CStringArray* args, int windowMode, int command) {
    int count = args->GetSize();
    if (count == 0) {
        net::ClientSession* server = Server();
        if (ServerActive(server)) {
            // Our binding's player record: only the host gets the window.
            auto* binding = static_cast<net::BindingMessage*>(server->binding_);
            if (static_cast<net::PlayerMessage*>(binding->PlayerRef().ptr)->host_)
                InterfaceActiveWindowOriginal(TheInterface())->VirtualPlayerList(windowMode);
            return;
        }
    }
    if (count != 1 || ArgIs(args, 0, "")) return;   // "" does nothing
    mfc::CString name((*args)[0]);
    const char* text = name;
    if (text[0] == '"' && text[Length(name) - 1] == '"') name = Ops(name).Mid(1, Length(name) - 2);
    if (ServerActive(Server())) Server()->SendCommand(command, name);
}
} // namespace

// 005E0590: kick [name].
void __fastcall Kick(CStringArray* args) { KickOrBan(args, 1, 2); }
EI_REPLACE(0x005E0590, Kick);

// 005E06E0: ban [name].
void __fastcall Ban(CStringArray* args) { KickOrBan(args, 2, 3); }
EI_REPLACE(0x005E06E0, Ban);

// 005E0830: fps 1|on|0|off (anything else: nothing).
void __fastcall Fps(CStringArray* args) {
    if (args->GetSize() < 1) return PrintLine("Invalid number of parameters for command \"FPS\"");
    if (ArgOn(args, 0)) TheInterface()->ShowFps(1);
    else if (ArgOff(args, 0)) TheInterface()->ShowFps(0);
}
EI_REPLACE(0x005E0830, Fps);

namespace {
// rate / localrate: bytes per second, clamped to 500..100000.
void SetRate(CStringArray* args, int* rate, const char* usage) {
    if (args->GetSize() != 1) return PrintLine(usage);
    int value = libcrt::Atoi((*args)[0]);
    if (value < 500) value = 500;
    else if (value > 100000) value = 100000;
    *rate = value;
}
} // namespace

// 005E0990: rate <bytes per second>.
void __fastcall Rate(CStringArray* args) { SetRate(args, ei::GamePointer<int>(0x00783E34), "Rate has one parameter - connection rate in bytes per second"); }
EI_REPLACE(0x005E0990, Rate);

// 005E0A10: localrate <bytes per second>.
void __fastcall LocalRate(CStringArray* args) { SetRate(args, ei::GamePointer<int>(0x00783E38), "LocalRate has one parameter - connection rate in bytes per second"); }
EI_REPLACE(0x005E0A10, LocalRate);

} // namespace console::commands
