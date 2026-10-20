#pragma once

// No harness run writes into a game install (verify build only; harness_install.cpp, WB17).
//
// The redirect: every module's file calls that write (CreateFile, CreateFile2, DeleteFile, MoveFile*,
// CopyFile*, CreateDirectory, RemoveDirectory, SetFileAttributes, ANSI and wide) come through kernel32's own
// import slots (its exports are stubs jumping through them, or call them), which the harness takes, as WB16
// took advapi32's: a write of an install file goes to the run's overlay (harness.cpp's WritePath: harness_saves
// <relative path>, saves and config\tutorial.ini as the game's own calls). So addon.dll's log (starter\
// addon.log) and crash dumps (starter\Dumps\), EI_Plugin.asi's, Miles' and Bink's writes land in the run
// folder like the game's (whose import slots harness.cpp takes first).
//
// The guard: ntdll's NtCreateFile, NtOpenFile, NtSetInformationFile (rename, link, delete) and NtDeleteFile,
// below every other layer. A file or folder under an install root (the working directory at start,
// EI_GAME_ROOT, EI_INSTALL_ROOTS: ei_launch's two installs) opened for writing, created, renamed, linked or
// deleted is refused (access denied) and fails the run ("install: write refused: ..." with the calling module),
// whatever made the call. A run that reports it found a write the redirect does not cover.

#include <cstddef>
#include <string>

namespace ei::verify::install {

// The run's overlay (harness.cpp), ANSI paths of MAX_PATH (260) characters.
struct Overlay {
    // The file a write of `path` goes to (`path` itself when it is not an install file); `keep`: the install's
    // file is copied into the overlay first.
    const char* (*write)(const char* path, char (&buffer)[260], bool keep);
    // The file a read of `path` opens (the overlay's copy when there is one, else `path`).
    const char* (*read)(const char* path, char (&buffer)[260]);
    // Whether `path` is an install file (under the game's root, not in the overlay).
    bool (*inInstall)(const char* path);
};

// Whether a CreateFile with this access and disposition (CREATE_ALWAYS, ...) writes or may create its file.
bool WritesFile(unsigned long access, unsigned long disposition);

// Takes kernel32's slots and ntdll's calls (once, at harness start, after the game's own slots).
void Install(const Overlay& overlay);

// The run's install problems (writes refused, calls the module could not take), "; "-separated; empty when
// there were none.
std::string Failure();

} // namespace ei::verify::install
