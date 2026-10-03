// Console command handlers (game.exe 0x005DD920-0x005E0A8B). Each gets the command's arguments,
// already lower-cased, in ECX. See docs/notes/console.md.
#include "ei/x87.h"
#include "console.h"

#include "../lib/crt.h"

#include "ei/hook.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console::commands {

namespace {

__declspec(naked) int __cdecl Atoi(const char*) EI_ORIGINAL(0x006E80D8)
__declspec(naked) double __cdecl Atof(const char*) EI_ORIGINAL(0x006E911A)

int Mbscmp(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
int Mbsicmp(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// Argument `i` equals `text` (_mbscmp).
bool ArgIs(const CStringArray* args, int i, std::uintptr_t text) { return Mbscmp((*args)[i], GameText(text)) == 0; }
// "1"/"on" and "0"/"off".
bool ArgOn(const CStringArray* args, int i) { return ArgIs(args, i, 0x00787C00) || ArgIs(args, i, 0x007925E4); }
bool ArgOff(const CStringArray* args, int i) { return ArgIs(args, i, 0x007856B8) || ArgIs(args, i, 0x0078D5E0); }

// 007C7D50: the interface manager.
struct InterfaceAt {
    void SetConsoleRect(int* rect);     // 005F7ED0 (console window +0x14, if any: 005E3E90)
    void SetConsoleHistory(int lines);  // 005F7EF0 (005E3F90)
    void SetDebugInfoRect(int* rect);   // 005F7F10 (00444C10 on 0079B930)
    void* TopWindow();                  // 005F7590
    void ShowFps(bool on);              // 005F5700
};
__declspec(naked) void InterfaceAt::SetConsoleRect(int*) EI_ORIGINAL(0x005F7ED0)
__declspec(naked) void InterfaceAt::SetConsoleHistory(int) EI_ORIGINAL(0x005F7EF0)
__declspec(naked) void InterfaceAt::SetDebugInfoRect(int*) EI_ORIGINAL(0x005F7F10)
__declspec(naked) void* InterfaceAt::TopWindow() EI_ORIGINAL(0x005F7590)
__declspec(naked) void InterfaceAt::ShowFps(bool) EI_ORIGINAL(0x005F5700)
inline InterfaceAt* Interface() { return reinterpret_cast<InterfaceAt*>(0x007C7D50); }

// 007AF5B8: the renderer's debug display flags (+0x88).
struct DebugDisplayAt {
    void FpsOn();               // 00569A10
    void FpsOff();              // 00569A20
    void RenderInfoOn();        // 00569A40
    void RenderInfoOff();       // 00569A50
    void ManagersOn();          // 00569A90
    void ManagersOff();         // 00569AA0
    void WorldDataOn();         // 00569B10
    void WorldDataOff();        // 00569B20
    char* LocalClient();        // 005699F0 (its +0x1C: the local player in a network game)
};
__declspec(naked) void DebugDisplayAt::FpsOn() EI_ORIGINAL(0x00569A10)
__declspec(naked) void DebugDisplayAt::FpsOff() EI_ORIGINAL(0x00569A20)
__declspec(naked) void DebugDisplayAt::RenderInfoOn() EI_ORIGINAL(0x00569A40)
__declspec(naked) void DebugDisplayAt::RenderInfoOff() EI_ORIGINAL(0x00569A50)
__declspec(naked) void DebugDisplayAt::ManagersOn() EI_ORIGINAL(0x00569A90)
__declspec(naked) void DebugDisplayAt::ManagersOff() EI_ORIGINAL(0x00569AA0)
__declspec(naked) void DebugDisplayAt::WorldDataOn() EI_ORIGINAL(0x00569B10)
__declspec(naked) void DebugDisplayAt::WorldDataOff() EI_ORIGINAL(0x00569B20)
__declspec(naked) char* DebugDisplayAt::LocalClient() EI_ORIGINAL(0x005699F0)
inline DebugDisplayAt* DebugDisplay() { return reinterpret_cast<DebugDisplayAt*>(0x007AF5B8); }

// 007AFF60: the world.
struct WorldAt {
    char* GetPlayer(int index);   // 00594180
    void LootAll();               // 00593D60
};
__declspec(naked) char* WorldAt::GetPlayer(int) EI_ORIGINAL(0x00594180)
__declspec(naked) void WorldAt::LootAll() EI_ORIGINAL(0x00593D60)
inline WorldAt* World() { return reinterpret_cast<WorldAt*>(0x007AFF60); }

// Player members used here: +0x20 its state object (quest variables, inventory, party), +0xA0 the
// party units (0x784 bytes each), +0xA4 their count, +0xC0 money, +0xC4 the global variables
// (hash_map<const char*, float>).
template <typename T>
T& Field(char* base, int offset) { return *reinterpret_cast<T*>(base + offset); }

struct PlayerStateAt {
    void SetVariable(const char* name, float value);   // 0066EE10
    void Refresh();                                    // 0066EF80
    void ResendParty();                                // 0066F450
};
__declspec(naked) void PlayerStateAt::SetVariable(const char*, float) EI_ORIGINAL(0x0066EE10)
__declspec(naked) void PlayerStateAt::Refresh() EI_ORIGINAL(0x0066EF80)
__declspec(naked) void PlayerStateAt::ResendParty() EI_ORIGINAL(0x0066F450)

struct UnitStatsAt {
    void AddExperience(float amount);   // 00523960
};
__declspec(naked) void UnitStatsAt::AddExperience(float) EI_ORIGINAL(0x00523960)

// The global variables' hash table: a node is {next, name, value}.
struct VariableNode {
    VariableNode* next;
    const char* name;
    float value;
};
struct VariableIterator {
    VariableNode* node;
    void* table;
    VariableNode* NextBucket();   // 005E0DB0: first node of the next non-empty bucket, or null
};
__declspec(naked) VariableNode* VariableIterator::NextBucket() EI_ORIGINAL(0x005E0DB0)
struct VariableTableAt {
    void Begin(VariableIterator* it);   // 004614C0
};
__declspec(naked) void VariableTableAt::Begin(VariableIterator*) EI_ORIGINAL(0x004614C0)

// 004DBE40: splits `text` at `separator` into `out`.
__declspec(naked) void __fastcall Split(const char*, CStringArray*, char) EI_ORIGINAL(0x004DBE40)

// 005DC620 (Rebirth patches it): runs a console line.
__declspec(naked) void __fastcall ExecuteAt(const char*) EI_ORIGINAL(0x005DC620)

// 0057F440 (with the game 007AFE20 in ECX): a network game is running.
__declspec(naked) int __fastcall NetworkGameAt(void*) EI_ORIGINAL(0x0057F440)

// 0079B5D8: the network server, active unless +4 has bit 31.
inline char* Server() { return *reinterpret_cast<char**>(0x0079B5D8); }
bool ServerActive(char* server) { return server && !(Field<unsigned>(server, 4) & 0x80000000u); }
struct ServerAt {
    void Command(int kind, const char* argument);   // 0041D260 (0 zone, 2 kick, 3 ban)
};
__declspec(naked) void ServerAt::Command(int, const char*) EI_ORIGINAL(0x0041D260)

// 007C7D68: the screen fader.
struct FaderAt {
    void FadeOut(int milliseconds);   // 005E6B90
};
__declspec(naked) void FaderAt::FadeOut(int) EI_ORIGINAL(0x005E6B90)

// 00500960: the timer (007AF060); vtable +0x18 gives the last frame time in milliseconds.
__declspec(naked) void* __cdecl FrameTimer() EI_ORIGINAL(0x00500960)

// MFC CTime (a time_t).
struct TimeAt {
    long time;
    TimeAt* Construct(int year, int month, int day, int hour, int minute, int second, int dst);   // 006FE3BB
};
__declspec(naked) TimeAt* TimeAt::Construct(int, int, int, int, int, int, int) EI_ORIGINAL(0x006FE3BB)

void SetRect(void (InterfaceAt::*set)(int*), int left, int top, int right, int bottom) {
    int rect[4] = {left, top, right, bottom};
    (Interface()->*set)(rect);
}

} // namespace

// 005DF5E0
void __fastcall Help(CStringArray*) {
    static constexpr std::uintptr_t kLines[] = {
        0x00792B30, 0x00792B28, 0x00792B14, 0x00792B04, 0x00792AB0, 0x00792AA0, 0x00792A44,
        0x00792A30, 0x00792A10, 0x007929F0, 0x00792998, 0x00792970, 0x00792928, 0x00792900,
        0x007928BC, 0x007928B4, 0x007928A4, 0x00792888, 0x0079285C, 0x00792850, 0x00792844,
    };
    for (std::uintptr_t line : kLines) PrintLine(GameText(line));
}
EI_REPLACE(0x005DF5E0, Help);

// 005DD920: debuginfo l<eft>|r<ight>|lt|lefttop|rt|righttop.
void __fastcall DebugInfo(CStringArray* args) {
    if (args->GetSize() != 0) {
        if (ArgIs(args, 0, 0x0079251C) || ArgIs(args, 0, 0x0078F9AC))   // left, l
            return SetRect(&InterfaceAt::SetDebugInfoRect, 10, 10, 0x122, 0x24E);
        if (ArgIs(args, 0, 0x00792514) || ArgIs(args, 0, 0x00792510))   // right, r
            return SetRect(&InterfaceAt::SetDebugInfoRect, 0x1FE, 10, 0x316, 0x24E);
        if (ArgIs(args, 0, 0x00792508) || ArgIs(args, 0, 0x00792504))   // lefttop, lt
            return SetRect(&InterfaceAt::SetDebugInfoRect, 10, 10, 0x122, 0x122);
        if (ArgIs(args, 0, 0x007924F8) || ArgIs(args, 0, 0x007849E4))   // righttop, rt
            return SetRect(&InterfaceAt::SetDebugInfoRect, 0x1FE, 10, 0x316, 0x122);
    }
    PrintLine(GameText(0x007924DC));   // "Invalid debug info position"
}
EI_REPLACE(0x005DD920, DebugInfo);

// 005DDBE0: console l<eft>|r<ight>|t<op>|b<ottom>|c<enter>|f<ullscreen> (800x600 coordinates).
void __fastcall ConsolePos(CStringArray* args) {
    if (args->GetSize() != 0) {
        if (ArgIs(args, 0, 0x0079251C) || ArgIs(args, 0, 0x0078F9AC))   // left, l
            return SetRect(&InterfaceAt::SetConsoleRect, 0, 0, 0xC8, 0x258);
        if (ArgIs(args, 0, 0x00792514) || ArgIs(args, 0, 0x00792510))   // right, r
            return SetRect(&InterfaceAt::SetConsoleRect, 0x258, 0, 0x320, 0x258);
        if (ArgIs(args, 0, 0x00792568) || ArgIs(args, 0, 0x00792564))   // top, t
            return SetRect(&InterfaceAt::SetConsoleRect, 0, 0, 0x320, 0x96);
        if (ArgIs(args, 0, 0x0079255C) || ArgIs(args, 0, 0x00792558))   // bottom, b
            return SetRect(&InterfaceAt::SetConsoleRect, 0, 0x1C2, 0x320, 0x258);
        if (ArgIs(args, 0, 0x00792550) || ArgIs(args, 0, 0x0079254C))   // center, c
            return SetRect(&InterfaceAt::SetConsoleRect, 0xC8, 0x96, 0x258, 0x1C2);
        if (ArgIs(args, 0, 0x00792540) || ArgIs(args, 0, 0x0078BBD8))   // fullscreen, f
            return SetRect(&InterfaceAt::SetConsoleRect, 0, 0, 0x320, 0x258);
    }
    PrintLine(GameText(0x00792524));   // "Invalid console position"
}
EI_REPLACE(0x005DDBE0, ConsolePos);

// 005DDF80: history <lines>.
void __fastcall History(CStringArray* args) {
    if (args->GetSize() == 0) return PrintLine(GameText(0x0079256C));   // "Invalid parameters"
    Interface()->SetConsoleHistory(Atoi((*args)[0]));
}
EI_REPLACE(0x005DDF80, History);

// 005DDFE0: filter (prints the shown categories), filter <type> (shows only that one),
// filter <type> 1|on|0|off.
void __fastcall Filter(CStringArray* args) {
    if (args->GetSize() == 0) {
        static constexpr std::uintptr_t kLabels[] = {0x007925DC, 0x007925D0, 0x007925C4, 0x007925BC, 0x007925B0, 0x007925A8};
        GameOStream& os = Out();
        for (int i = 0; i < 6; ++i)   // "AI: on", "\nEVENT: off", ...
            os << GameText(kLabels[i]) << GameText(FilterMask() & (1u << i) ? 0x007925E4 : 0x0078D5E0);
        return EndLine(os);
    }
    unsigned type;
    if (ArgIs(args, 0, 0x007846F0)) type = 0;               // none
    else if (ArgIs(args, 0, 0x007925A4)) type = 1;          // ai
    else if (ArgIs(args, 0, 0x0079259C)) type = 2;          // event
    else if (ArgIs(args, 0, 0x00792590)) type = 4;          // graphics
    else if (ArgIs(args, 0, 0x0079258C)) type = 8;          // rpg
    else if (ArgIs(args, 0, 0x00792580)) type = 0x10;       // unitacks
    else if (ArgIs(args, 0, 0x00792438)) type = 0x20;       // net
    else if (ArgIs(args, 0, 0x0078FE08)) type = ~0u;        // all
    else return PrintLine(GameText(0x0079256C));            // "Invalid parameters"
    if (args->GetSize() <= 1) {
        FilterMask() = type;
    } else if (ArgOn(args, 1)) {
        FilterMask() |= type;
    } else if (ArgOff(args, 1)) {
        FilterMask() &= ~type;
    } else {
        PrintLine(GameText(0x0079256C));
    }
}
EI_REPLACE(0x005DDFE0, Filter);

// 005DE460: show fps|managers|render_info|world_data 1|on|0|off (anything else: nothing).
void __fastcall Show(CStringArray* args) {
    if (args->GetSize() < 2) return PrintLine(GameText(0x00792634));   // 'Invalid number of parameters for command "show"'
    void (DebugDisplayAt::*on)();
    void (DebugDisplayAt::*off)();
    if (ArgIs(args, 0, 0x007923F8)) {            // fps
        on = &DebugDisplayAt::FpsOn;
        off = &DebugDisplayAt::FpsOff;
    } else if (ArgIs(args, 0, 0x00792628)) {     // managers
        on = &DebugDisplayAt::ManagersOn;
        off = &DebugDisplayAt::ManagersOff;
    } else if (ArgIs(args, 0, 0x0079261C)) {     // render_info
        on = &DebugDisplayAt::RenderInfoOn;
        off = &DebugDisplayAt::RenderInfoOff;
    } else if (ArgIs(args, 0, 0x00792610)) {     // world_data
        on = &DebugDisplayAt::WorldDataOn;
        off = &DebugDisplayAt::WorldDataOff;
    } else {
        return PrintLine(GameText(0x007925E8));   // "Invalid parameters in command 'show'"
    }
    if (ArgOn(args, 1)) (DebugDisplay()->*on)();
    else if (ArgOff(args, 1)) (DebugDisplay()->*off)();
}
EI_REPLACE(0x005DE460, Show);

// 005DEA00: listvar <player> [file]: prints the player's global variables ("name = value") and
// writes them to the file ("%s %g") when one is given. In a network game the local player is used.
void __fastcall ListVar(CStringArray* args) {
    if (args->GetSize() == 0) return PrintLine(GameText(0x007926A4));   // usage
    char* player;
    if (NetworkGameAt(reinterpret_cast<void*>(0x007AFE20)) & 0xFF)
        player = Field<char*>(DebugDisplay()->LocalClient(), 0x1C);
    else
        player = World()->GetPlayer(Atoi((*args)[0]));
    if (!player) return PrintLine(GameText(0x00792690));   // "Wrong player number"

    mfc::CString line;
    CStdioFile file;
    mfc::BOOL writing = 0;
    if (args->GetSize() > 1) {
        writing = file.Open((*args)[1], CStdioFile::modeCreate | CStdioFile::modeWrite, nullptr);
        if (!writing)   // "Cannot open file <name> for writing"
            EndLine(Out() << GameText(0x0079266C) << static_cast<const char*>((*args)[1]) << GameText(0x00792680));
    }
    VariableIterator it;
    reinterpret_cast<VariableTableAt*>(player + 0xC4)->Begin(&it);
    while (it.node) {
        EndLine((Out() << it.node->name << GameText(0x0078DA6C)).PutFloat(it.node->value));   // " = "
        if (writing) {
            line.Format(GameText(0x00792664), it.node->name, static_cast<double>(it.node->value));   // "%s %g\n"
            file.WriteString(line);
        }
        VariableNode* next = it.node->next;
        if (!next) next = it.NextBucket();
        it.node = next;
    }
    if (writing) file.Close();
}
EI_REPLACE(0x005DEA00, ListVar);

// 005DEC90: loadvar <player> <file>: sets the variables of each "name value" line.
void __fastcall LoadVar(CStringArray* args) {
    if (args->GetSize() < 2) return PrintLine(GameText(0x007926E4));   // usage
    char* player = World()->GetPlayer(Atoi((*args)[0]));
    if (!player) return PrintLine(GameText(0x00792690));   // "Wrong player number"
    auto* state = Field<PlayerStateAt*>(player, 0x20);
    if (!state) return PrintLine(GameText(0x00792690));

    mfc::CString line;
    CStringArray parts;
    CStdioFile file;
    if (!file.Open((*args)[1], CStdioFile::modeRead, nullptr)) {   // "Cannot open file <name> for reading"
        EndLine(Out() << GameText(0x0079266C) << static_cast<const char*>((*args)[1]) << GameText(0x007926D4));
        return;
    }
    while (file.ReadString(line)) {
        Split(line, &parts, ' ');
        if (parts.GetSize() > 1) {
            const char* name = parts[0];
            float value = static_cast<float>(Atof(parts[1]));
            state->SetVariable(name, value);
        }
    }
    file.Close();
}
EI_REPLACE(0x005DEC90, LoadVar);

// 005DEEE0: "Current FPS: " 1000 / last frame time (printed as float).
void __fastcall LastFps(CStringArray*) {
    void* timer = FrameTimer();
    GameOStream& os = Out() << GameText(0x00792710);
    using FrameTimeFn = unsigned(__thiscall*)(void*);
    unsigned ms = (*reinterpret_cast<FrameTimeFn**>(timer))[0x18 / 4](timer);
    double fps = *reinterpret_cast<const float*>(0x0073C34C) / x87::FromUnsigned(ms);
    EndLine(os.PutFloat(fps));
}
EI_REPLACE(0x005DEEE0, LastFps);

// 005DEF50: exec <file>: runs each line of the file as a console command. Quotes around the name
// are trimmed (the message names the argument as given).
void __fastcall Exec(CStringArray* args) {
    mfc::CString line;
    mfc::CString name;
    if (args->GetSize() == 0) return PrintLine(GameText(0x00792720));   // usage
    name = (*args)[0];
    Ops(name).TrimLeft('"');
    Ops(name).TrimRight('"');
    CStdioFile file;
    if (!file.Open(name, CStdioFile::typeText, nullptr)) {   // "Cannot open file <name> for reading"
        EndLine(Out() << GameText(0x0079266C) << static_cast<const char*>((*args)[0]) << GameText(0x007926D4));
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
        PrintLine(GameText(0x007927A4));   // "IMPORTANT: use only from global map"
        PrintLine(GameText(0x00792784));   // "give [NPlayer] money [NMoney]"
        return PrintLine(GameText(0x00792768));
    }
    char* player = World()->GetPlayer(Atoi((*args)[0]));
    if (!player) return PrintLine(GameText(0x00792690));   // "Wrong player number"
    auto* state = Field<PlayerStateAt*>(player, 0x20);
    if (!state) return PrintLine(GameText(0x00792690));
    if (ArgIs(args, 1, 0x00792760) && args->GetSize() > 2) {   // money
        int amount = Atoi((*args)[2]);
        Field<int>(player, 0xC0) += amount;
        return state->Refresh();
    }
    if (ArgIs(args, 1, 0x0079275C) && args->GetSize() > 2) {   // exp
        int count = Field<int>(player, 0xA4);
        // Rounded to float once, as the original stores it (volatile: /fp:fast would keep a double).
        volatile float share = static_cast<float>(Atof((*args)[2]) / count);
        for (int i = 0; i < count; ++i)
            reinterpret_cast<UnitStatsAt*>(Field<char*>(player, 0xA0) + i * 0x784 + 0x20)->AddExperience(share);
        return state->Refresh();
    }
    if (ArgIs(args, 1, 0x00792750)) {   // assortment
        void* window = Interface()->TopWindow();
        using OpenFn = void(__thiscall*)(void*);
        (*reinterpret_cast<OpenFn**>(window))[0x9C / 4](window);
        return;
    }
    PrintLine(GameText(0x00792744));   // "Give what?"
}
EI_REPLACE(0x005DF0F0, Give);

// 005DF5B0: lootall.
void __fastcall LootAll(CStringArray*) {
    PrintLine(GameText(0x00792838));   // "Looting..."
    World()->LootAll();
}
EI_REPLACE(0x005DF5B0, LootAll);

// 005DF8E0: fadeout <milliseconds>.
void __fastcall FadeOut(CStringArray* args) {
    if (args->GetSize() <= 0) return PrintLine(GameText(0x00792B40));   // "FADEOUT nMillSec"
    FaderAt* fader = *reinterpret_cast<FaderAt**>(0x007C7D68);
    fader->FadeOut(Atoi((*args)[0]));
    PrintLine(GameText(0x00792B54));   // "Ok"
}
EI_REPLACE(0x005DF8E0, FadeOut);

// 005DF9B0: net client|server|time 0/1 (traffic and timing output), net zone <name>.
void __fastcall Net(CStringArray* args) {
    if (args->GetSize() != 2) {
        PrintLine(GameText(0x00792BC8));
        PrintLine(GameText(0x00792BA0));
        return PrintLine(GameText(0x00792B70));
    }
    if (Mbsicmp((*args)[0], GameText(0x00792B68)) == 0) {   // client
        mfc::CString value((*args)[1]);
        char* client = Field<char*>(DebugDisplay()->LocalClient(), 0x1C);
        Field<int>(client, 0x10C) = Atoi(value);
    }
    if (Mbsicmp((*args)[0], GameText(0x00792450)) == 0) {   // server
        if (char* player = World()->GetPlayer(0)) Field<int>(player, 0x10C) = Atoi((*args)[1]);
    }
    if (Mbsicmp((*args)[0], GameText(0x00792B60)) == 0)     // time
        *reinterpret_cast<int*>(0x0079BC60) = Atoi((*args)[1]);
    if (Mbsicmp((*args)[0], GameText(0x00792B58)) == 0 && ServerActive(Server())) {   // zone
        mfc::CString zone((*args)[1]);
        reinterpret_cast<ServerAt*>(Server())->Command(0, zone);
    }
}
EI_REPLACE(0x005DF9B0, Net);

// 005E00D0: days ddmmyyyy DDMMYYYY: the days between two dates (06:00:00 and 07:01:01 local time).
void __fastcall Days(CStringArray* args) {
    auto digits = [&](int i) {
        for (int k = 0; k < 8; ++k) {
            signed char c = static_cast<const char*>((*args)[i])[k];
            if (c < '0' || c > '9') return false;
        }
        return true;
    };
    auto number = [&](int i, int first, int count) { return Atoi(Ops((*args)[i]).Mid(first, count)); };
    if (args->GetSize() >= 2 && Length((*args)[0]) >= 8 && Length((*args)[1]) >= 8 && digits(0) && digits(1)) {
        int year = number(0, 4, 4);
        if (year >= 1900) {
            int month = number(0, 2, 2);
            if (month >= 1 && month <= 12) {
                int day = number(0, 0, 2);
                if (day >= 1 && day <= 31) {
                    TimeAt birth;
                    long from = birth.Construct(year, month, day, 6, 0, 0, -1)->time;
                    int year2 = number(1, 4, 4);
                    if (year2 >= 1900) {
                        int month2 = number(1, 2, 2);
                        if (month2 >= 1 && month2 <= 12) {
                            int day2 = number(1, 0, 2);
                            if (day2 >= 1 && day2 <= 31) {
                                TimeAt now;
                                long to = now.Construct(year2, month2, day2, 7, 1, 1, -1)->time;
                                int days = static_cast<int>((to - from) / 86400);
                                EndLine(Out() << GameText(0x00792CA0) << days << GameText(0x00792CAC));   // "Your age: " " days"
                                return;
                            }
                        }
                    }
                }
            }
        }
    }
    static constexpr std::uintptr_t kUsage[] = {0x00792C7C, 0x00792C60, 0x00792C48, 0x00792C3C, 0x00792C20, 0x00792C0C};
    for (std::uintptr_t line : kUsage) PrintLine(GameText(line));
}
EI_REPLACE(0x005E00D0, Days);

// 005E0570: partyresend.
void __fastcall PartyResend(CStringArray*) {
    if (char* player = World()->GetPlayer(0)) Field<PlayerStateAt*>(player, 0x20)->ResendParty();
}
EI_REPLACE(0x005E0570, PartyResend);

namespace {
// kick / ban: without arguments the server's player list window (if the server allows it), with a
// name (quotes stripped) the server command.
void KickOrBan(CStringArray* args, int windowMode, int command) {
    int count = args->GetSize();
    if (count == 0) {
        char* server = Server();
        if (ServerActive(server)) {
            if (*(Field<char*>(Field<char*>(server, 0x25F0), 0xC) + 0x28)) {
                void* window = Interface()->TopWindow();
                using OpenFn = void(__thiscall*)(void*, int);
                (*reinterpret_cast<OpenFn**>(window))[0x98 / 4](window, windowMode);
            }
            return;
        }
    }
    if (count != 1 || ArgIs(args, 0, 0x0079B298)) return;   // "" does nothing
    mfc::CString name((*args)[0]);
    const char* text = name;
    if (text[0] == '"' && text[Length(name) - 1] == '"') name = Ops(name).Mid(1, Length(name) - 2);
    if (ServerActive(Server())) reinterpret_cast<ServerAt*>(Server())->Command(command, name);
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
    if (args->GetSize() < 1) return PrintLine(GameText(0x00792CB4));   // 'Invalid number of parameters for command "FPS"'
    if (ArgOn(args, 0)) Interface()->ShowFps(true);
    else if (ArgOff(args, 0)) Interface()->ShowFps(false);
}
EI_REPLACE(0x005E0830, Fps);

namespace {
// rate / localrate: bytes per second, clamped to 500..100000.
void SetRate(CStringArray* args, int* rate, std::uintptr_t usage) {
    if (args->GetSize() != 1) return PrintLine(GameText(usage));
    int value = Atoi((*args)[0]);
    if (value < 500) value = 500;
    else if (value > 100000) value = 100000;
    *rate = value;
}
} // namespace

// 005E0990: rate <bytes per second>.
void __fastcall Rate(CStringArray* args) { SetRate(args, reinterpret_cast<int*>(0x00783E34), 0x00792CE4); }
EI_REPLACE(0x005E0990, Rate);

// 005E0A10: localrate <bytes per second>.
void __fastcall LocalRate(CStringArray* args) { SetRate(args, reinterpret_cast<int*>(0x00783E38), 0x00792D24); }
EI_REPLACE(0x005E0A10, LocalRate);

} // namespace console::commands
