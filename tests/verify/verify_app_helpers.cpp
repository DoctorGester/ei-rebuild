// Checks for app_helpers.cpp (A-E1). All run on the main thread.
//  - DiskFreeSpace: side by side; the free space can change between the two calls (another process
//    writing), so a difference is checked against a second run of the original. Reached by the
//    save and load screens (ui_saveload).
//  - SetQuitAllowed: side by side (the original, then ours; the byte must hold the value). Reached
//    by a movie (render_movie).
//  - MovieMode, CEngineObject::World: pure, side by side. World is reached by every scenario with
//    units; MovieMode by the credits screen (none of the scenarios).
//  - QuitGame: ours only (it posts the quit message); the result against the flag. Reached when a
//    scenario ends through the menu's exit or the console's quit.
//  - DelayedProtectionCheck: ours only (it draws from rand); the check list must be empty, as
//    nothing fills it. Reached by every new object of the client. ScrambleObjects: ours only; no
//    scenario reaches it (the list is empty).
//  - CEngineObject::ReleaseAndDelete: ours only (it can delete the object); an object that stays
//    has one reference less. Reached when a 3D object's attachment is replaced (map_objects).
//  - ConstructProtectionChecks: the static initialiser runs it before the hooks, so a start-up
//    check runs the original and ours on the global (its list put back after): an empty list, the
//    same sentinel from the pool.
#include "verify.h"

#include "../../src/game/app_helpers.h"
#include "../../src/game/engine_object.h"
#include "../../src/lib/stlp_alloc.h"

#include <cstdint>

namespace {
void* s_unused;

// 00455A90: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x24 (3).
double(__fastcall* s_diskFree)(const char*);
double __fastcall DiskFreeVerify(const char* path) {
    double expected = s_diskFree(path);
    double actual = app::DiskFreeSpace(path);
    if (expected != actual) expected = s_diskFree(path);
    ei::verify::CheckNear("app::DiskFreeSpace", expected, actual, "%s", path ? path : "(null)");
    return actual;
}

// 00455F10: MOV [00784751],CL (6).
void(__fastcall* s_setQuit)(std::uint8_t);
void __fastcall SetQuitVerify(std::uint8_t allowed) {
    s_setQuit(allowed);
    std::uint8_t expected = app::QuitAllowed();
    app::SetQuitAllowed(allowed);
    ei::verify::Check("app::SetQuitAllowed", expected, app::QuitAllowed(), "%d", allowed);
}

bool __fastcall QuitVerify(int exitCode) {
    bool allowed = app::QuitAllowed() != 0;
    bool result = app::QuitGame(exitCode);
    ei::verify::Check("app::QuitGame", allowed, result, "code %d", exitCode);
    return result;
}

// 00455E20: MOV AL,[00784752] (5).
std::uint8_t(__cdecl* s_movieMode)();
std::uint8_t __cdecl MovieModeVerify() {
    std::uint8_t expected = s_movieMode();
    std::uint8_t actual = app::MovieMode();
    ei::verify::Check("app::MovieMode", expected, actual, "");
    return actual;
}

void __cdecl CheckVerify() {
    bool empty = app::ProtectionChecks().Empty();
    app::DelayedProtectionCheck();
    ei::verify::Check("app::DelayedProtectionCheck", true, empty, "the check list has entries");
}

void __cdecl ScrambleVerify() {
    app::ScrambleObjects();
    ei::verify::Check("app::ScrambleObjects", true, true, "");
}

// 00455950: MOV EAX,[ECX+0x14] (3), MOV ECX,[EAX+0xAC] (6).
void*(__fastcall* s_world)(const CEngineObject*);
void* __fastcall WorldVerify(const CEngineObject* self) {
    void* expected = s_world(self);
    void* actual = self->World();
    ei::verify::Check("CEngineObject::World", expected, actual, "object %p", static_cast<const void*>(self));
    return actual;
}

void __fastcall ReleaseVerify(CEngineObject* self) {
    int before = self->refCount_;
    self->ReleaseAndDelete();
    ei::verify::Check("CEngineObject::ReleaseAndDelete", true, before == 1 || self->refCount_ == before - 1, "refs %d",
                      before);
}
// 00455E50: PUSH -1 (2), PUSH imm32 (5).
void(__stdcall* s_constructChecks)();
bool EmptyList(const app::ProtectionCheckList& list) {
    return list.head && list.head->next == list.head && list.head->prev == list.head;
}
void __stdcall ConstructChecksVerify() {
    app::ConstructProtectionChecks();
    ei::verify::Check("app::ConstructProtectionChecks", true, EmptyList(app::ProtectionChecks()), "");
}
void ConstructChecksStartupCheck() {
    app::ProtectionCheckList& checks = app::ProtectionChecks();
    app::ProtectionCheckList::Node* kept = checks.head;
    s_constructChecks();
    app::ProtectionCheckList::Node* expected = checks.head;
    bool expectedEmpty = EmptyList(checks);
    stlp::deallocate(expected, sizeof(*expected));
    app::ConstructProtectionChecks();
    app::ProtectionCheckList::Node* actual = checks.head;
    bool ok = expectedEmpty && EmptyList(checks) && actual == expected;
    stlp::deallocate(actual, sizeof(*actual));
    checks.head = kept;
    ei::verify::Check("app::ConstructProtectionChecks", true, ok, "sentinel %p, original %p",
                      static_cast<const void*>(actual), static_cast<const void*>(expected));
}
} // namespace

EI_STARTUP_CHECK(ConstructChecksStartupCheck);
EI_VERIFY(0x00455E50, ConstructChecksVerify, 7, s_constructChecks);
EI_VERIFY(0x00455A90, DiskFreeVerify, 6, s_diskFree);
EI_VERIFY(0x00455F10, SetQuitVerify, 6, s_setQuit);
EI_VERIFY(0x00455F20, QuitVerify, 0, s_unused);
EI_VERIFY(0x00455E20, MovieModeVerify, 5, s_movieMode);
EI_VERIFY(0x00455B50, CheckVerify, 0, s_unused);
EI_VERIFY(0x00455BA0, ScrambleVerify, 0, s_unused);
EI_VERIFY(0x00455950, WorldVerify, 9, s_world);
EI_VERIFY(0x00457720, ReleaseVerify, 0, s_unused);
