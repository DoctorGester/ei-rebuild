// The music system (CMusicSystem, world/app object + 0x1A8): chooses what the sound manager's MP3 stream
// plays for the game state. In a zone (state 1) it plays a combat track of the allod's theme while the
// party fights (combat state == 2) and otherwise, after a random pause of 120..180 s, a calm track; the
// briefing, constructor, main menu and credits states each play one file. The themes come from music.reg
// (LoadThemes).
#include "sound.h"
#include "game_strings.h"

#include "console.h"
#include "registry.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "../lib/text_case.h"
#include "random.h"

#include "game_singletons.h"
#include "lib/str.h"
#include "world_data.h"
#include "world.h"

#include <cstring>
#include <new>
#include <string>


namespace {

// The themes' registry file and the music files' extension.
constexpr const char* kMusicReg = /*00744090*/ "music.reg";
constexpr const char* kMp3 = /*00744094*/ ".mp3";

// Next delay of the calm music: 120..180 s from now. The random value is drawn first.
std::uint32_t CalmDelay() {
    std::uint32_t r = g_random.Next() % 60001;
    return gameapi::timeGetTime() + r + 120000;
}

// One name of a theme list: the only one, or one at random.
const std::string& PickName(const std::vector<std::string>& names) {
    std::uint32_t n = static_cast<std::uint32_t>(names.size());
    if (n == 1) return names[0];
    return names[g_random.Next() % n];
}

// Waits for the stream thread to take up a request (state 2 -> 1), then asks it to stop after
// that file.
void WaitAndEndAfter(CSoundManager* snd) {
    while (snd->streamState == 2) gameapi::Sleep(0);
    snd->StopStream(1);
}

// ---- Theme loading ----


// PlayBriefing / PlayConstructor: the allod's file at `offset` in its theme, if the allod has one.
void PlayThemeFile(CMusicSystem* music, std::string_view allod, std::string MusicTheme::*file) {
    auto theme = std::ranges::find_if(music->themes, [allod](const MusicTheme& t) {
        return text::CompareIgnoringCase(t.name, allod) == 0;
    });
    if (theme == music->themes.end()) return;
    std::string path = (*theme).*file + kMp3;
    g_clientWorld->sounds_->RequestStream(path, 4);
}

// "CMusicSystem".
const char* MusicClassNameText() { return "CMusicSystem"; }

} // namespace

// The error message names the file without its path, and its text is FatalError's
// format string (original).
int CMusicSystem::LoadThemes() {
    CFileRegistry reg;
    reg.Construct();
    bool opened;
    {
        std::string folder = *g_textFolder;
        std::string path = folder + kMusicReg;
        opened = reg.Open(path);
    }
    if (!opened) {
        int result;
        {
            std::string file = kMusicReg;
            std::string message = "CMusicSystem::Create()\nUnable to open file \"" + file;   // "CMusicSystem::Create()\nUnable to open file \""
            std::string full = message + "\"";    // "\""
            result = game::FatalError(full);
        }
        reg.Destroy();
        return result;
    }
    const char* const common = "Common";
    credits = reg.GetString(common, "Credits", std::string(game::kEmptyText));
    mainMenu = reg.GetString(common, "MainMenu", std::string(game::kEmptyText));
    {
        std::vector<std::string> sections;
        reg.GetSectionNames(&sections);
        for (const std::string& name : sections) {
            const char* section = name.c_str();
            if (text::CompareIgnoringCase(section, "common") == 0) continue;
            MusicTheme read;
            read.name = name;
            read.dungeon = 0;
            read.briefing = reg.GetString(section, "Briefing", std::string(game::kEmptyText));
            read.constructor = reg.GetString(section, "Constructor", std::string(game::kEmptyText));
            reg.GetStringArray(section, "CalmOpen", &read.calm);
            reg.GetStringArray(section, "Combat", &read.combat);
            themes.push_back(read);
            read.dungeon = 1;
            reg.GetStringArray(section, "CalmDungeon", &read.calm);
            themes.push_back(read);
        }
    }
    reg.Destroy();
    return 1;
}

// Entering a zone: picks the allod's open land or dungeon theme; a missing theme is
// fatal. The calm music is due at once (calmDue 1) after the first world frame clears `holding`.
void CMusicSystem::SetZoneMode(std::string_view allod, int dungeon) {
    g_clientWorld->sounds_->StreamNotify();
    g_clientWorld->sounds_->StopStream(4);
    holding = 1;
    for (int i = 0; i < std::ssize(themes); ++i) {
        if (text::CompareIgnoringCase(themes[i].name, allod) == 0 && themes[i].dungeon == dungeon) {
            theme = i;
            state = 1;
            calmDue = 1;
            g_clientWorld->sounds_->StopStream(4);
            inCombat = 0;
            combatTrack = game::kEmptyText;
            return;
        }
    }
    std::string name(allod);
    std::string message = "CMusicSystem::SetZoneMode()\nUnable to find sound theme for allod \"" + name;
    std::string full = message + "\"";
    game::FatalError(full);
}

void CMusicSystem::Stop() {
    state = 0;
    g_clientWorld->sounds_->StopStream(4);
}

void CMusicSystem::PlayBriefing(std::string_view allod) {
    state = 2;
    g_clientWorld->sounds_->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::briefing);
}

void CMusicSystem::PlayConstructor(std::string_view allod) {
    state = 3;
    g_clientWorld->sounds_->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::constructor);
}

void CMusicSystem::PlayMainMenu() {
    state = 4;
    g_clientWorld->sounds_->StopStream(4);
    std::string path = mainMenu + kMp3;
    g_clientWorld->sounds_->RequestStream(path, 4);
}

// A one-off piece in a zone (the PlayMusic script command); the calm/combat choice
// waits until it has finished.
void CMusicSystem::PlayJingle(std::string_view name) {
    if (state != 1) return;
    {
        std::string path(name);
        path += kMp3;
        jingle = path;
    }
    g_clientWorld->sounds_->RequestStream(jingle, 4);
    WaitAndEndAfter(g_clientWorld->sounds_);
    jinglePlaying = 1;
}

// Every world frame. Always 1.
int CMusicSystem::UpdateMusic() {
    CSoundManager* snd = g_clientWorld->sounds_;
    if (state != 1 || holding == 1) return 1;
    std::string current;
    {
        std::string playing = snd->CurrentStreamName();
        current = playing;
    }
    if (jinglePlaying != 0) {
        if (str::Compare(current, jingle) == 0) return 1;
        jinglePlaying = 0;
        inCombat = 0;
        calmDue = CalmDelay();
    }
    int fighting = g_clientWorld->partyDanger_ == 2 ? 1 : 0;
    if (inCombat != 0) {
        if (fighting == 0) {
            snd->StopStream(2);
            calmDue = CalmDelay();
        }
    } else if (fighting == 1) {
        if (str::Compare(current, combatTrack) != 0 || str::Compare(combatTrack, game::kEmptyText) == 0) {
            combatTrack = PickName(themes[theme].combat);
            combatTrack += kMp3;
        }
        snd->RequestStream(combatTrack, 3);
    } else if (str::Compare(current, game::kEmptyText) == 0) {
        combatTrack = game::kEmptyText;
        if (calmDue == 0) {
            std::uint32_t r = g_random.Next();
            calmDue = gameapi::timeGetTime() + r % 60001 + 120000;
        }
        if (gameapi::timeGetTime() >= calmDue) {
            calmDue = 0;
            const MusicTheme& t = themes[theme];
            if (!t.calm.empty()) {
                std::string track;
                track = PickName(t.calm);
                track += kMp3;
                snd->RequestStream(track, 4);
                WaitAndEndAfter(snd);
            }
        }
    }
    inCombat = fighting;
    return 1;
}

// ---- Lifetime and the engine object's virtuals ----

CMusicSystem::CMusicSystem() {
    ClearFields();
    ResetThemes();
}

void CMusicSystem::ResetThemes() {
    state = 0;
    themes.clear();
    jingle = game::NoneText();
    mainMenu = jingle;
    credits = mainMenu;
    jinglePlaying = 0;
    calmDue = 0;
    inCombat = 0;
    holding = 0;
    combatTrack = game::NoneText();
}

// The strings go after the body (jingle, combatTrack, mainMenu, credits: the game's order, before its theme
// array's destructor).
CMusicSystem::~CMusicSystem() {
    ResetThemes();
}

void CMusicSystem::FadeOut() { g_clientWorld->sounds_->FadeOutStream(); }
void CMusicSystem::FadeIn() { g_clientWorld->sounds_->FadeInStream(); }

// ClassName and GetClassId.
const char* CMusicSystem::ClassName() { return MusicClassNameText(); }
ClassId CMusicSystem::GetClassId() { return kClassMusicSystem; }
