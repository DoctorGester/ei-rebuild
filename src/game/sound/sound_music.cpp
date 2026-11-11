// The music system (MusicSystem): chooses what the sound manager's MP3 stream
// plays for the game state. In a zone it plays a combat track of the allod's theme while the
// party fights (party danger 2) and otherwise, after a random pause of 120..180 s, a calm track; the
// briefing, constructor, main menu and credits states each play one file. The themes come from music.reg
// (Create).
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "engine/core/random.h"
#include "engine/data/registry.h"
#include "game/sound/sound.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

namespace {

// The themes' registry file and the music files' extension.
constexpr const char* kMusicReg = /*00744090*/ "music.reg";
constexpr const char* kMp3 = /*00744094*/ ".mp3";

// Next delay of the calm music: 120..180 s from now. The random value is drawn first.
std::uint32_t CalmDelay() {
    std::uint32_t r = g_clientRandom.Next() % 60001;
    return platform::FrameClock() + r + 120000;
}

// One name of a theme list: the only one, or one at random; an empty list gives an empty name.
std::string_view PickName(const std::vector<std::string>& names) {
    std::uint32_t n = static_cast<std::uint32_t>(names.size());
    if (n == 0) return {};
    if (n == 1) return names[0];
    return names[g_clientRandom.Next() % n];
}

// The first theme of the allod, or null.
const MusicTheme* FindTheme(const MusicSystem& music, std::string_view allod) {
    auto theme = std::ranges::find_if(music.themes, [allod](const MusicTheme& t) {
        return text::CompareIgnoringCase(t.name, allod) == 0;
    });
    return theme != music.themes.end() ? &*theme : nullptr;
}

} // namespace

void MusicSystem::Create() {
    FileRegistry reg;
    if (!reg.Open(g_textFolder + kMusicReg))
        game::FatalExit("CMusicSystem::Create()\nUnable to open file \"%s\"", kMusicReg);
    const char* const common = "Common";
    credits = reg.GetString(common, "Credits", {});
    mainMenu = reg.GetString(common, "MainMenu", {});
    for (const std::string& name : reg.SectionNames()) {
        if (text::CompareIgnoringCase(name, "common") == 0) continue;
        MusicTheme read;
        read.name = name;
        read.dungeon = false;
        read.briefing = reg.GetString(name, "Briefing", {});
        read.constructor = reg.GetString(name, "Constructor", {});
        reg.GetStringArray(name, "CalmOpen", &read.calm);
        reg.GetStringArray(name, "Combat", &read.combat);
        themes.push_back(read);
        read.dungeon = true;
        reg.GetStringArray(name, "CalmDungeon", &read.calm);
        themes.push_back(read);
    }
}

// Entering a zone: picks the allod's open land or dungeon theme; without one (an allod music.reg does not name) a
// warning, and the music state stays as it was. The calm music is due at once (calmDue 1) after the first world frame
// clears `holding`.
void MusicSystem::SetZoneMode(std::string_view allod, bool dungeon) {
    g_clientWorld.sounds->StopStream(StreamSwitch::Now);
    holding = true;
    for (int i = 0; i < std::ssize(themes); ++i) {
        if (text::CompareIgnoringCase(themes[i].name, allod) == 0 && themes[i].dungeon == dungeon) {
            theme = i;
            state = MusicState::Zone;
            calmDue = 1;
            inCombat = false;
            combatTrack.clear();
            return;
        }
    }
    game::Warning("CMusicSystem::SetZoneMode(): no sound theme for allod \"%s\"%s", std::string(allod),
                  dungeon ? " (dungeon)" : "");
}

void MusicSystem::Stop() {
    state = MusicState::Off;
    g_clientWorld.sounds->StopStream(StreamSwitch::Now);
}

void MusicSystem::PlayBriefing(std::string_view allod) {
    state = MusicState::Briefing;
    g_clientWorld.sounds->StopStream(StreamSwitch::Now);
    if (const MusicTheme* found = FindTheme(*this, allod))
        g_clientWorld.sounds->RequestStream(found->briefing + kMp3, StreamSwitch::Now);
}

void MusicSystem::PlayConstructor(std::string_view allod) {
    state = MusicState::Constructor;
    g_clientWorld.sounds->StopStream(StreamSwitch::Now);
    if (const MusicTheme* found = FindTheme(*this, allod))
        g_clientWorld.sounds->RequestStream(found->constructor + kMp3, StreamSwitch::Now);
}

void MusicSystem::PlayMainMenu() {
    state = MusicState::MainMenu;
    g_clientWorld.sounds->StopStream(StreamSwitch::Now);
    g_clientWorld.sounds->RequestStream(mainMenu + kMp3, StreamSwitch::Now);
}

// A one-off piece in a zone (the PlayMusic script command); the calm/combat choice
// waits until it has finished.
void MusicSystem::PlayJingle(std::string_view name) {
    if (state != MusicState::Zone) return;
    jingle = std::string(name) + kMp3;
    g_clientWorld.sounds->PlayStreamOnce(jingle);
    jinglePlaying = true;
}

// Every world frame.
void MusicSystem::UpdateMusic() {
    SoundManager* snd = g_clientWorld.sounds.get();
    // Nothing is chosen while a jingle or calm track waits for the stream's next step: the stream still names the file
    // before it.
    if (state != MusicState::Zone || holding || snd->StreamSwitchPending()) return;
    const std::string current = snd->CurrentStreamName();
    if (jinglePlaying) {
        if (str::Compare(current, jingle) == 0) return;
        jinglePlaying = false;
        inCombat = false;
        calmDue = CalmDelay();
    }
    const bool fighting = g_clientWorld.partyDanger == 2;
    if (inCombat) {
        if (!fighting) {
            snd->StopStream(StreamSwitch::AfterFade);
            calmDue = CalmDelay();
        }
    } else if (fighting) {
        if (str::Compare(current, combatTrack) != 0 || combatTrack.empty()) {
            combatTrack = PickName(themes[theme].combat);
            combatTrack += kMp3;
        }
        snd->RequestStream(combatTrack, StreamSwitch::AtMarker);
    } else if (current.empty()) {
        combatTrack.clear();
        if (calmDue == 0) calmDue = CalmDelay();
        if (platform::FrameClock() >= calmDue) {
            calmDue = 0;
            const MusicTheme& t = themes[theme];
            if (!t.calm.empty()) snd->PlayStreamOnce(std::string(PickName(t.calm)) + kMp3);
        }
    }
    inCombat = fighting;
}

// ---- Lifetime and the engine object's virtuals ----

MusicSystem::MusicSystem() {
    ResetThemes();
}

void MusicSystem::ResetThemes() {
    state = MusicState::Off;
    themes.clear();
    jingle = "none";
    mainMenu = jingle;
    credits = mainMenu;
    jinglePlaying = false;
    calmDue = 0;
    inCombat = false;
    holding = false;
    combatTrack = "none";
}

// The strings go after the body (jingle, combatTrack, mainMenu, credits: the game's order, before its theme
// array's destructor).
MusicSystem::~MusicSystem() {
    ResetThemes();
}

const char* MusicSystem::ClassName() { return "CMusicSystem"; }
ClassId MusicSystem::GetClassId() { return ClassId::MusicSystem; }
