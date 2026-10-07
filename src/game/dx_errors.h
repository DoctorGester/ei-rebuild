#pragma once

// 00480B80 (A-S8): the text of a DirectDraw / Direct3D / COM result, as the game's error reports
// print it (FatalErrorCode 004800E0 and 00480300). dx_errors.cpp.

// ECX = the result. Every known code's message (each ending in a newline), "No error.\n" for 0,
// "Unrecognized error value.\n" for any other. Ours returns its own copies of the exe's strings.
const char* __fastcall DirectXErrorText(long result);
