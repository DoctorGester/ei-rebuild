#pragma once

// The music system's theme array, CArray<MusicTheme> (sound_music.cpp; S16, on the standalone boot
// path): 006B59A0 SetSize, 006B5BC0 / 006B5B50 ConstructElements / DestructElements (the zero fill,
// then the three strings empty and the two name arrays constructed; the element destructor on each)
// and 006B4D90 the element destructor (the arrays, then the strings, last member first).

#include "sound.h"

#include "../lib/mfc_array.h"

void InitMusicTheme(MusicTheme* theme);
void __fastcall DestructMusicTheme(MusicTheme* theme);
using MusicThemes =
    mfc::impl::ArrayAbi<MusicTheme, mfc::impl::EachElements<MusicTheme, InitMusicTheme, DestructMusicTheme>>;
