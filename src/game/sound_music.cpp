// The music system (CMusicSystem, world/app object + 0x1A8; 006B4E30-006B56BA): chooses what the
// sound manager's MP3 stream plays for the game state. In a zone (state 1) it plays a combat
// track of the allod's theme while the party fights (007AFC9C == 2) and otherwise, after a random
// pause of 120..180 s, a calm track; the briefing, constructor, main menu and credits states each
// play one file. The themes come from music.reg (LoadThemes, 006B4710).
#include "sound.h"
#include "game_strings.h"

#include "console.h"
#include "registry.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "random.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "record_arrays.h"
#include "game_data.h"
#include "game_singletons.h"
#include "world_data.h"

#include <new>
#include <string>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

// The themes' registry file and the music files' extension.
constexpr const char* kMusicReg = /*00744090*/ "music.reg";
constexpr const char* kMp3 = /*00744094*/ ".mp3";

int Compare(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}
int CompareNoCase(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b);
}

// Next delay of the calm music: 120..180 s from now. The random value is drawn first.
std::uint32_t CalmDelay() {
    std::uint32_t r = IsaacRandom::Global().Next() % 60001;
    return gameapi::timeGetTime() + r + 120000;
}

// One name of a theme list: the only one, or one at random.
const mfc::CString& PickName(const SoundNameArray& names) {
    std::uint32_t n = static_cast<std::uint32_t>(names.size);
    if (n == 1) return names.data[0];
    return names.data[IsaacRandom::Global().Next() % n];
}

// Waits for the stream thread to take up a request (state 2 -> 1), then asks it to stop after
// that file.
void WaitAndEndAfter(CSoundManager* snd) {
    while (snd->streamState == 2) gameapi::Sleep(0);
    snd->StopStream(1);
}

// ---- Theme loading ----


// The themes' registry (registry.cpp).
using Registry = CFileRegistry;

// The theme array (CArray<MusicTheme> at +0x1C): SetSize [006B59A0: size, grow by (-1 keeps it)]
// and the element assignment [006B4D40] (MFC template instantiations; the original inlines the
// second SetSize and the first assignment, identically).
void ThemesSetSize(void* themes, int size) {
    static_cast<recordarrays::Themes::Array*>(themes)->SetSize(size, -1);
}

// The theme being read, laid out as MusicTheme with constructed members.
struct LocalTheme {
    mfc::CString name;
    std::int32_t dungeon = 0;
    mfc::CString briefing;
    mfc::CString constructor;
    console::CStringArray calm;
    console::CStringArray combat;
};
static_assert(sizeof(LocalTheme) == sizeof(MusicTheme));

// Appends a copy of `theme` to the music system's themes.
void AppendTheme(CMusicSystem* music, const LocalTheme& theme) {
    int n = music->themeCount;
    ThemesSetSize(&music->themesVtable, n + 1);
    music->themes[n].Assign(reinterpret_cast<const MusicTheme*>(&theme));
}

const mfc::CString& TextPath() { return TheTextFolder(); }

} // namespace

// The error message names the file without its path, and its text is FatalError's
// format string (original).
int CMusicSystem::LoadThemes() {
    Registry reg;
    reg.Construct();
    mfc::BOOL opened;
    {
        std::string folder = TextPath().GetString();
        std::string path = folder + kMusicReg;
        opened = reg.Open(path.c_str());
    }
    if (!opened) {
        int result;
        {
            std::string file = kMusicReg;
            std::string message = "CMusicSystem::Create()\nUnable to open file \"" + file;   // "CMusicSystem::Create()\nUnable to open file \""
            std::string full = message + "\"";    // "\""
            result = game::FatalError(full.c_str());
        }
        reg.Destroy();
        return result;
    }
    const char* const common = "Common";
    credits = reg.GetString(common, "Credits", mfc::CString(game::EmptyText()));
    mainMenu = reg.GetString(common, "MainMenu", mfc::CString(game::EmptyText()));
    {
        console::CStringArray sections;
        reg.GetSectionNames(&sections);
        for (int i = 0; i < sections.size; ++i) {
            const char* section = sections.data[i];
            if (CompareNoCase(section, "common") == 0) continue;
            LocalTheme read;
            read.name = sections.data[i];
            read.dungeon = 0;
            read.briefing = reg.GetString(section, "Briefing", mfc::CString(game::EmptyText()));
            read.constructor = reg.GetString(section, "Constructor", mfc::CString(game::EmptyText()));
            reg.GetStringArray(section, "CalmOpen", &read.calm);
            reg.GetStringArray(section, "Combat", &read.combat);
            AppendTheme(this, read);
            read.dungeon = 1;
            reg.GetStringArray(section, "CalmDungeon", &read.calm);
            AppendTheme(this, read);
        }
    }
    reg.Destroy();
    return 1;
}
EI_REWRITTEN(0x006B4710, CMusicSystem::LoadThemes);

// Entering a zone: picks the allod's open land or dungeon theme; a missing theme is
// fatal. The calm music is due at once (calmDue 1) after the first world frame clears `holding`.
void CMusicSystem::SetZoneMode(const char* allod, int dungeon) {
    TheSoundManager()->StreamNotify(4);
    TheSoundManager()->StopStream(4);
    holding = 1;
    for (int i = 0; i < themeCount; ++i) {
        if (CompareNoCase(themes[i].name, allod) == 0 && themes[i].dungeon == dungeon) {
            theme = i;
            state = 1;
            calmDue = 1;
            TheSoundManager()->StopStream(4);
            inCombat = 0;
            combatTrack = game::EmptyText();
            return;
        }
    }
    std::string name = allod;
    std::string message = "CMusicSystem::SetZoneMode()\nUnable to find sound theme for allod \"" + name;
    std::string full = message + "\"";
    game::FatalError(full.c_str());
}
EI_REWRITTEN(0x006B4E30, CMusicSystem::SetZoneMode);

void CMusicSystem::Stop() {
    state = 0;
    TheSoundManager()->StopStream(4);
}
EI_REWRITTEN(0x006B4F70, CMusicSystem::Stop);

namespace {
// PlayBriefing / PlayConstructor: the allod's file at `offset` in its theme, if the allod has one.
void PlayThemeFile(CMusicSystem* music, const char* allod, mfc::CString MusicTheme::*file) {
    std::string name;
    int i = 0;
    for (; i < music->themeCount; ++i) {
        if (CompareNoCase(music->themes[i].name, allod) == 0) {
            name = music->themes[i].*file;
            break;
        }
    }
    if (i == music->themeCount) return;
    std::string path = name + kMp3;
    TheSoundManager()->RequestStream(path.c_str(), 4);
}
} // namespace

void CMusicSystem::PlayBriefing(const char* allod) {
    state = 2;
    TheSoundManager()->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::briefing);
}
EI_REWRITTEN(0x006B4F90, CMusicSystem::PlayBriefing);

void CMusicSystem::PlayConstructor(const char* allod) {
    state = 3;
    TheSoundManager()->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::constructor);
}
EI_REWRITTEN(0x006B5080, CMusicSystem::PlayConstructor);

void CMusicSystem::PlayCredits() {
    state = 5;
    TheSoundManager()->StopStream(4);
    std::string path = (credits + kMp3).GetString();
    TheSoundManager()->RequestStream(path.c_str(), 4);
}
EI_REWRITTEN(0x006B5170, CMusicSystem::PlayCredits);

void CMusicSystem::PlayMainMenu() {
    state = 4;
    TheSoundManager()->StopStream(4);
    std::string path = (mainMenu + kMp3).GetString();
    TheSoundManager()->RequestStream(path.c_str(), 4);
}
EI_REWRITTEN(0x006B51F0, CMusicSystem::PlayMainMenu);

// A one-off piece in a zone (the PlayMusic script command); the calm/combat choice
// waits until it has finished.
void CMusicSystem::PlayJingle(const char* name) {
    if (state != 1) return;
    {
        std::string stem = name;
        std::string path = stem + kMp3;
        jingle = path;
    }
    TheSoundManager()->RequestStream(jingle, 4);
    WaitAndEndAfter(TheSoundManager());
    jinglePlaying = 1;
}
EI_REWRITTEN(0x006B5270, CMusicSystem::PlayJingle);

// 006B5350 (vtable slot 6, every world frame). Always 1.
int CMusicSystem::UpdateMusic() {
    CSoundManager* snd = TheSoundManager();
    if (state != 1 || holding == 1) return 1;
    std::string current;
    {
        std::string playing = snd->CurrentStreamName().GetString();
        current = playing;
    }
    if (jinglePlaying != 0) {
        if (Compare(current.c_str(), jingle) == 0) return 1;
        jinglePlaying = 0;
        inCombat = 0;
        calmDue = CalmDelay();
    }
    int fighting = worlddata::FightState() == 2 ? 1 : 0;
    if (inCombat != 0) {
        if (fighting == 0) {
            snd->StopStream(2);
            calmDue = CalmDelay();
        }
    } else if (fighting == 1) {
        if (Compare(current.c_str(), combatTrack) != 0 || Compare(combatTrack, game::EmptyText()) == 0) {
            combatTrack = PickName(themes[theme].combat);
            combatTrack += kMp3;
        }
        snd->RequestStream(combatTrack, 3);
    } else if (Compare(current.c_str(), game::EmptyText()) == 0) {
        combatTrack = game::EmptyText();
        if (calmDue == 0) {
            std::uint32_t r = IsaacRandom::Global().Next();
            calmDue = gameapi::timeGetTime() + r % 60001 + 120000;
        }
        if (gameapi::timeGetTime() >= calmDue) {
            calmDue = 0;
            const MusicTheme& t = themes[theme];
            if (t.calm.size != 0) {
                std::string track;
                track = PickName(t.calm);
                track += kMp3;
                snd->RequestStream(track.c_str(), 4);
                WaitAndEndAfter(snd);
            }
        }
    }
    inCombat = fighting;
    return 1;
}
EI_REWRITTEN(0x006B5350, CMusicSystem::UpdateMusic);

// ---- Lifetime and the engine object's slots (A-H13) ----

namespace {
// The theme array's destructor (CArray<MusicTheme>::~CArray, a template instantiation).
void ThemesDestruct(void* themes) {
    recordarrays::Themes::Destruct<recordarrays::ThemeArrayClass>(static_cast<recordarrays::Themes::Array*>(themes));
}
constexpr int kMusicClassId = 0xE;
// "CMusicSystem".
const char* MusicClassNameText() { return "CMusicSystem"; }
} // namespace

CMusicSystem::CMusicSystem() {
    ClearFields();
    themesVtable = recordarrays::ThemeArrayClass::Vtable();   // game.exe: 007440CC (the same slots)
    themes = nullptr;
    themeGrow = 0;
    themeMax = 0;
    themeCount = 0;
    ResetThemes();
}
EI_REWRITTEN_SPECIAL(0x006B4350, CMusicSystem::CMusicSystem);

void CMusicSystem::ResetThemes() {
    state = 0;
    ThemesSetSize(&themesVtable, 0);
    jingle = game::NoneText();
    mainMenu = jingle;
    credits = mainMenu;
    jinglePlaying = 0;
    calmDue = 0;
    inCombat = 0;
    holding = 0;
    combatTrack = game::NoneText();
}
EI_REWRITTEN(0x006B4690, CMusicSystem::ResetThemes);

// The strings go after the body (jingle, combatTrack, mainMenu, credits: the original's order
// before its theme array's destructor).
CMusicSystem::~CMusicSystem() {
    ResetThemes();
    ThemesDestruct(&themesVtable);
}
EI_REWRITTEN_SPECIAL(0x006B44B0, CMusicSystem::~CMusicSystem);
EI_REWRITTEN_SPECIAL(0x006B4490, CMusicSystem::~CMusicSystem);   // deleting

void CMusicSystem::FadeOut() { TheSoundManager()->FadeOutStream(); }
void CMusicSystem::FadeIn() { TheSoundManager()->FadeInStream(); }
EI_REWRITTEN(0x006B4E10, CMusicSystem::FadeOut);
EI_REWRITTEN(0x006B4E20, CMusicSystem::FadeIn);

void MusicTheme::Assign(const MusicTheme* from) {
    name = from->name;
    dungeon = from->dungeon;
    briefing = from->briefing;
    constructor = from->constructor;
    calm.Copy(from->calm);
    combat.Copy(from->combat);
}
EI_REWRITTEN(0x006B4D40, MusicTheme::Assign);

// 006B4470 / 006B4480: slots 11 and 12.
const char* CMusicSystem::GetClassName() { return MusicClassNameText(); }
int CMusicSystem::GetClassId() { return kMusicClassId; }
