// Small application helpers of 00455950-00457720 (A-E1): the free disk space, the quit flag, the
// movie mode byte, the copy protection's delayed check and two CEngineObject helpers. See
// docs/notes/app.md.
#include "app_helpers.h"

#include "base_manager.h"
#include "camera.h"
#include "engine_object.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "options.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>

namespace app {

namespace {

// 00784788 "kernel32.dll", 00784798 "GetDiskFreeSpaceExA".
const char* Kernel32Text() { return "kernel32.dll"; }
const char* GetDiskFreeSpaceExText() { return "GetDiskFreeSpaceExA"; }

using GetDiskFreeSpaceExFn = BOOL(__stdcall*)(const char*, ULARGE_INTEGER*, ULARGE_INTEGER*, ULARGE_INTEGER*);

// rand() scaled to 0 .. n (the original's rand() * n / 0x7FFF).
int Scaled(int n) { return gamecrt::rand() * n / 0x7FFF; }

} // namespace

// 00455A90 (path in ECX; the result in ST0). The bytes free for the caller on the path's drive:
// GetDiskFreeSpaceExA when kernel32 has it (looked up on every call), else sectors per cluster x
// bytes per sector x free clusters. 0 when the call fails.
double __fastcall DiskFreeSpace(const char* path) {
    auto getEx = reinterpret_cast<GetDiskFreeSpaceExFn>(
        gameapi::GetProcAddress(gameapi::GetModuleHandleA(Kernel32Text()), GetDiskFreeSpaceExText()));
    if (getEx) {
        ULARGE_INTEGER freeForCaller, total, totalFree;
        if (!getEx(path, &freeForCaller, &total, &totalFree)) return 0.0;
        return static_cast<double>(static_cast<std::int64_t>(freeForCaller.QuadPart));   // FILD qword: signed
    }
    DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
    if (!gameapi::GetDiskFreeSpaceA(path, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) return 0.0;
    return x87::FromUnsigned(sectorsPerCluster) * x87::FromUnsigned(bytesPerSector) * x87::FromUnsigned(freeClusters);
}

// 00455F10 (the value in CL). Whether QuitGame posts the quit message (cleared while a timed
// movie plays).
void __fastcall SetQuitAllowed(std::uint8_t allowed) { QuitAllowed() = allowed; }

// 00455F20 (the exit code in ECX). Posts WM_QUIT when quitting is allowed; returns whether it did.
bool __fastcall QuitGame(int exitCode) {
    if (!QuitAllowed()) return false;
    gameapi::PostQuitMessage(exitCode);
    return true;
}

// 00455E20. The movie mode byte (00784752; 1 unless the command line changed it).
std::uint8_t __cdecl MovieMode() { return MovieModeFlag(); }

// 00455BA0. The copy protection's sabotage: the options go back to their defaults and every object
// of the object manager gets a random number below 0x1000 as its companion and as its specific
// half (both are pointers; the game fails some time later).
void __cdecl ScrambleObjects() {
    options::OptionsResetDefaults(options::TheOptions());
    ObjectListIterator it{};
    for (CEngineObject* object = it.First(TheObjectManager()); object; object = it.Next()) {
        static_cast<CMapObject*>(object)->AttachCompanion(reinterpret_cast<CEngineObject*>(Scaled(0x1000) % 0x1000));
        reinterpret_cast<CObject3D*>(object)->SetSpecific(reinterpret_cast<CObject3DSpecific*>(Scaled(0x1000) % 0x1000));
    }
    if (it.manager) --it.manager->refCount_;
}

// 00455B50 (called for every new object of the client, CPlayerClient::OnNewObject; ECX is not
// read). One call in a hundred looks through the protection's check list (0079BC2C) and scrambles
// the objects when an entry has failed (its byte is 0). Nothing in the exe adds an entry, so the
// list is empty.
void __cdecl DelayedProtectionCheck() {
    if (Scaled(100) % 100 >= 1) return;
    ProtectionCheckList& checks = ProtectionChecks();
    for (ProtectionCheckList::Node* node = checks.First(); node != checks.End(); node = node->next) {
        if (!node->data) {
            ScrambleObjects();
            return;
        }
    }
}

// 00455E50 (the static initialiser 00455E30's constructor): the check list built empty, its
// sentinel from the node pool.
void __stdcall ConstructProtectionChecks() { ProtectionChecks().Construct(); }

} // namespace app

// 00455950.
CWorld* CEngineObject::World() const {
    auto* context = static_cast<CManagerContext*>(owner_->context_);
    return context ? context->ThisWorld() : nullptr;
}

// 00457720.
void CEngineObject::ReleaseAndDelete() {
    if (--refCount_ == 0) delete this;
}

EI_REPLACE(0x00455A90, app::DiskFreeSpace);
EI_REPLACE(0x00455F10, app::SetQuitAllowed);
EI_REPLACE(0x00455F20, app::QuitGame);
EI_REPLACE(0x00455E20, app::MovieMode);
EI_REPLACE(0x00455BA0, app::ScrambleObjects);
EI_REPLACE(0x00455B50, app::DelayedProtectionCheck);
EI_REPLACE(0x00455E50, app::ConstructProtectionChecks);
EI_REPLACE_METHOD(0x00455950, CEngineObject::World);
EI_REPLACE_METHOD(0x00457720, CEngineObject::ReleaseAndDelete);
