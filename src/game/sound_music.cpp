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
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "record_arrays.h"
#include "game_data.h"
#include "game_singletons.h"

#include <new>

namespace {

// The party's fight state, set by the world (2: fighting).
int FightState() { return ei::GameData<const volatile int>(0x007AFC9C); }
// ".mp3" (the pointer at 00744094).
const char* Mp3() { return ei::GameData<const char* const>(0x00744094); }

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

// ---- Theme loading (006B4710) ----


// The themes' registry (registry.cpp).
using Registry = CFileRegistry;

// The theme array (CArray<MusicTheme> at +0x1C): SetSize [006B59A0: size, grow by (-1 keeps it)]
// and the element assignment [006B4D40] (MFC template instantiations; the original inlines the
// second SetSize and the first assignment, identically).
void ThemesSetSize(void* themes, int size) {
    recordarrays::Themes::SetSize(static_cast<recordarrays::Themes::Array*>(themes), nullptr, size, -1);
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

const mfc::CString& TextPath() { return ei::GameData<const mfc::CString>(0x007AF45C); }
// "music.reg" (the pointer at 00744090).
const char* MusicReg() { return ei::GameData<const char* const>(0x00744090); }

} // namespace

// 006B4710. The error message names the file without its path, and its text is FatalError's
// format string (original).
int CMusicSystem::LoadThemes() {
    Registry reg;
    reg.Construct();
    mfc::BOOL opened;
    {
        mfc::CString folder(TextPath());
        mfc::CString path = folder + MusicReg();
        opened = reg.Open(path);
    }
    if (!opened) {
        int result;
        {
            mfc::CString file(MusicReg());
            mfc::CString message = "CMusicSystem::Create()\nUnable to open file \"" + file;   // "CMusicSystem::Create()\nUnable to open file \""
            mfc::CString full = message + "\"";    // "\""
            result = game::FatalError(static_cast<const char*>(full));
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
EI_REPLACE_METHOD(0x006B4710, CMusicSystem::LoadThemes);

// 006B4E30. Entering a zone: picks the allod's open land or dungeon theme; a missing theme is
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
    mfc::CString name(allod);
    mfc::CString message = "CMusicSystem::SetZoneMode()\nUnable to find sound theme for allod \"" + name;
    mfc::CString full = message + "\"";
    game::FatalError(static_cast<const char*>(full));
}
EI_REPLACE_METHOD(0x006B4E30, CMusicSystem::SetZoneMode);

// 006B4F70.
void CMusicSystem::Stop() {
    state = 0;
    TheSoundManager()->StopStream(4);
}
EI_REPLACE_METHOD(0x006B4F70, CMusicSystem::Stop);

namespace {
// PlayBriefing / PlayConstructor: the allod's file at `offset` in its theme, if the allod has one.
void PlayThemeFile(CMusicSystem* music, const char* allod, mfc::CString MusicTheme::*file) {
    mfc::CString name;
    int i = 0;
    for (; i < music->themeCount; ++i) {
        if (CompareNoCase(music->themes[i].name, allod) == 0) {
            name = music->themes[i].*file;
            break;
        }
    }
    if (i == music->themeCount) return;
    mfc::CString path = name + Mp3();
    TheSoundManager()->RequestStream(path, 4);
}
} // namespace

// 006B4F90.
void CMusicSystem::PlayBriefing(const char* allod) {
    state = 2;
    TheSoundManager()->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::briefing);
}
EI_REPLACE_METHOD(0x006B4F90, CMusicSystem::PlayBriefing);

// 006B5080.
void CMusicSystem::PlayConstructor(const char* allod) {
    state = 3;
    TheSoundManager()->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::constructor);
}
EI_REPLACE_METHOD(0x006B5080, CMusicSystem::PlayConstructor);

// 006B5170.
void CMusicSystem::PlayCredits() {
    state = 5;
    TheSoundManager()->StopStream(4);
    mfc::CString path = credits + Mp3();
    TheSoundManager()->RequestStream(path, 4);
}
EI_REPLACE_METHOD(0x006B5170, CMusicSystem::PlayCredits);

// 006B51F0.
void CMusicSystem::PlayMainMenu() {
    state = 4;
    TheSoundManager()->StopStream(4);
    mfc::CString path = mainMenu + Mp3();
    TheSoundManager()->RequestStream(path, 4);
}
EI_REPLACE_METHOD(0x006B51F0, CMusicSystem::PlayMainMenu);

// 006B5270. A one-off piece in a zone (the PlayMusic script command); the calm/combat choice
// waits until it has finished.
void CMusicSystem::PlayJingle(const char* name) {
    if (state != 1) return;
    {
        mfc::CString stem(name);
        mfc::CString path = stem + Mp3();
        jingle = path;
    }
    TheSoundManager()->RequestStream(jingle, 4);
    WaitAndEndAfter(TheSoundManager());
    jinglePlaying = 1;
}
EI_REPLACE_METHOD(0x006B5270, CMusicSystem::PlayJingle);

// 006B5350 (vtable slot 6, every world frame). Always 1.
int CMusicSystem::UpdateMusic() {
    CSoundManager* snd = TheSoundManager();
    if (state != 1 || holding == 1) return 1;
    mfc::CString current;
    {
        mfc::CString playing = snd->CurrentStreamName();
        current = playing;
    }
    if (jinglePlaying != 0) {
        if (Compare(current, jingle) == 0) return 1;
        jinglePlaying = 0;
        inCombat = 0;
        calmDue = CalmDelay();
    }
    int fighting = FightState() == 2 ? 1 : 0;
    if (inCombat != 0) {
        if (fighting == 0) {
            snd->StopStream(2);
            calmDue = CalmDelay();
        }
    } else if (fighting == 1) {
        if (Compare(current, combatTrack) != 0 || Compare(combatTrack, game::EmptyText()) == 0) {
            combatTrack = PickName(themes[theme].combat);
            combatTrack += Mp3();
        }
        snd->RequestStream(combatTrack, 3);
    } else if (Compare(current, game::EmptyText()) == 0) {
        combatTrack = game::EmptyText();
        if (calmDue == 0) {
            std::uint32_t r = IsaacRandom::Global().Next();
            calmDue = gameapi::timeGetTime() + r % 60001 + 120000;
        }
        if (gameapi::timeGetTime() >= calmDue) {
            calmDue = 0;
            const MusicTheme& t = themes[theme];
            if (t.calm.size != 0) {
                mfc::CString track;
                track = PickName(t.calm);
                track += Mp3();
                snd->RequestStream(track, 4);
                WaitAndEndAfter(snd);
            }
        }
    }
    inCombat = fighting;
    return 1;
}
EI_REPLACE_METHOD(0x006B5350, CMusicSystem::UpdateMusic);

// ---- Lifetime and the engine object's slots (A-H13) ----

namespace {
// 006B56C0: the theme array's destructor (CArray<MusicTheme>::~CArray, a template instantiation).
void ThemesDestruct(void* themes) {
    recordarrays::Themes::Destruct<recordarrays::ThemeArrayClass>(static_cast<recordarrays::Themes::Array*>(themes));
}
constexpr int kMusicClassId = 0xE;
// "CMusicSystem" (007967F4).
const char* MusicClassNameText() { return "CMusicSystem"; }
} // namespace

CMusicSystem* CMusicSystem::Construct() {
    ClearFields();
    ei::SetVtable(this, CEngineObject::kVtableAddress);
    themesVtable = ei::GamePointer<void>(kThemeArrayVtable);
    themes = nullptr;
    themeGrow = 0;
    themeMax = 0;
    themeCount = 0;
    new (&credits) mfc::CString();
    new (&mainMenu) mfc::CString();
    new (&combatTrack) mfc::CString();
    new (&jingle) mfc::CString();
    ei::SetVtable(this, kVtableAddress);
    ResetThemes();
    return this;
}
EI_REPLACE_METHOD(0x006B4350, CMusicSystem::Construct);

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
EI_REPLACE_METHOD(0x006B4690, CMusicSystem::ResetThemes);

void CMusicSystem::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    ResetThemes();
    jingle.~CString();
    combatTrack.~CString();
    mainMenu.~CString();
    credits.~CString();
    ThemesDestruct(&themesVtable);
    DestructBase();
}
EI_REPLACE_METHOD(0x006B44B0, CMusicSystem::DestructBody);

CMusicSystem* CMusicSystem::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006B4490, CMusicSystem::DeletingDestruct);

void CMusicSystem::FadeOut() { TheSoundManager()->FadeOutStream(); }
void CMusicSystem::FadeIn() { TheSoundManager()->FadeInStream(); }
EI_REPLACE_METHOD(0x006B4E10, CMusicSystem::FadeOut);
EI_REPLACE_METHOD(0x006B4E20, CMusicSystem::FadeIn);

void MusicTheme::Assign(const MusicTheme* from) {
    name = from->name;
    dungeon = from->dungeon;
    briefing = from->briefing;
    constructor = from->constructor;
    calm.Copy(from->calm);
    combat.Copy(from->combat);
}
EI_REPLACE_METHOD(0x006B4D40, MusicTheme::Assign);

// 006B4470 / 006B4480: slots 11 and 12.
const char* __fastcall MusicSystemClassName(CMusicSystem*) { return MusicClassNameText(); }
int __fastcall MusicSystemClassId(CMusicSystem*) { return kMusicClassId; }
EI_REPLACE(0x006B4470, MusicSystemClassName);
EI_REPLACE(0x006B4480, MusicSystemClassId);
