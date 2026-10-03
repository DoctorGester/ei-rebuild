// The music system (CMusicSystem, world/app object + 0x1A8; 006B4E30-006B56BA): chooses what the
// sound manager's MP3 stream plays for the game state. In a zone (state 1) it plays a combat
// track of the allod's theme while the party fights (007AFC9C == 2) and otherwise, after a random
// pause of 120..180 s, a calm track; the briefing, constructor, main menu and credits states each
// play one file. Theme loading from music.reg (006B4710) stays original.
#include "sound.h"

#include "../lib/crt.h"
#include "../lib/gameapi.h"
#include "random.h"

#include "ei/hook.h"

namespace {

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
// The party's fight state, set by the world (2: fighting).
int FightState() { return *reinterpret_cast<const volatile int*>(0x007AFC9C); }
const char* EmptyString() { return reinterpret_cast<const char*>(0x0079B298); }
// ".mp3" (the pointer at 00744094).
const char* Mp3() { return *reinterpret_cast<const char* const*>(0x00744094); }

int Compare(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
int CompareNoCase(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// Next delay of the calm music: 120..180 s from now. The random value is drawn first.
std::uint32_t CalmDelay() {
    std::uint32_t r = GlobalRandom().Next() % 60001;
    return gameapi::timeGetTime() + r + 120000;
}

// One name of a theme list: the only one, or one at random.
const mfc::CString& PickName(const SoundNameArray& names) {
    std::uint32_t n = static_cast<std::uint32_t>(names.size);
    if (n == 1) return names.data[0];
    return names.data[GlobalRandom().Next() % n];
}

// Waits for the stream thread to take up a request (state 2 -> 1), then asks it to stop after
// that file.
void WaitAndEndAfter(CSoundManager* snd) {
    while (snd->streamState == 2) gameapi::Sleep(0);
    snd->StopStream(1);
}

} // namespace

// 006B4E30. Entering a zone: picks the allod's open land or dungeon theme; a missing theme is
// fatal. The calm music is due at once (calmDue 1) after the first world frame clears `holding`.
void CMusicSystem::SetZoneMode(const char* allod, int dungeon) {
    sound::Manager()->StreamNotify(4);
    sound::Manager()->StopStream(4);
    holding = 1;
    for (int i = 0; i < themeCount; ++i) {
        if (CompareNoCase(themes[i].name, allod) == 0 && themes[i].dungeon == dungeon) {
            theme = i;
            state = 1;
            calmDue = 1;
            sound::Manager()->StopStream(4);
            inCombat = 0;
            combatTrack = EmptyString();
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
    sound::Manager()->StopStream(4);
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
    sound::Manager()->RequestStream(path, 4);
}
} // namespace

// 006B4F90.
void CMusicSystem::PlayBriefing(const char* allod) {
    state = 2;
    sound::Manager()->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::briefing);
}
EI_REPLACE_METHOD(0x006B4F90, CMusicSystem::PlayBriefing);

// 006B5080.
void CMusicSystem::PlayConstructor(const char* allod) {
    state = 3;
    sound::Manager()->StopStream(4);
    PlayThemeFile(this, allod, &MusicTheme::constructor);
}
EI_REPLACE_METHOD(0x006B5080, CMusicSystem::PlayConstructor);

// 006B5170.
void CMusicSystem::PlayCredits() {
    state = 5;
    sound::Manager()->StopStream(4);
    mfc::CString path = credits + Mp3();
    sound::Manager()->RequestStream(path, 4);
}
EI_REPLACE_METHOD(0x006B5170, CMusicSystem::PlayCredits);

// 006B51F0.
void CMusicSystem::PlayMainMenu() {
    state = 4;
    sound::Manager()->StopStream(4);
    mfc::CString path = mainMenu + Mp3();
    sound::Manager()->RequestStream(path, 4);
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
    sound::Manager()->RequestStream(jingle, 4);
    WaitAndEndAfter(sound::Manager());
    jinglePlaying = 1;
}
EI_REPLACE_METHOD(0x006B5270, CMusicSystem::PlayJingle);

// 006B5350 (vtable slot 6, every world frame). Always 1.
int CMusicSystem::UpdateMusic() {
    CSoundManager* snd = sound::Manager();
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
        if (Compare(current, combatTrack) != 0 || Compare(combatTrack, EmptyString()) == 0) {
            combatTrack = PickName(themes[theme].combat);
            combatTrack += Mp3();
        }
        snd->RequestStream(combatTrack, 3);
    } else if (Compare(current, EmptyString()) == 0) {
        combatTrack = EmptyString();
        if (calmDue == 0) {
            std::uint32_t r = GlobalRandom().Next();
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
