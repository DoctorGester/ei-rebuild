// Small application helpers: the free disk space, the quit flag, the movie mode byte, the copy
// protection's delayed check and two CEngineObject helpers. See docs/notes/app.md.
#include "app_helpers.h"

#include "app.h"
#include "base_manager.h"
#include "camera.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "options.h"
#include "unit_create.h"   // CMapObjectManager
#include "world.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/x87.h"

#include <cstdint>
#include <memory>

namespace app {

namespace {

const char* Kernel32Text() { return "kernel32.dll"; }
const char* GetDiskFreeSpaceExText() { return "GetDiskFreeSpaceExA"; }

using GetDiskFreeSpaceExFn = BOOL(__stdcall*)(const char*, ULARGE_INTEGER*, ULARGE_INTEGER*, ULARGE_INTEGER*);


} // namespace

// The bytes free for the caller on the path's drive: GetDiskFreeSpaceExA when kernel32 has it (looked up on
// every call), else sectors per cluster x bytes per sector x free clusters. 0 when the call fails.
double DiskFreeSpace(const char* path) {
    auto getEx = reinterpret_cast<GetDiskFreeSpaceExFn>(
        gameapi::GetProcAddress(gameapi::GetModuleHandleA(Kernel32Text()), GetDiskFreeSpaceExText()));
    if (getEx) {
        ULARGE_INTEGER freeForCaller, total, totalFree;
        if (!getEx(path, &freeForCaller, &total, &totalFree)) return 0.0;
        return static_cast<double>(static_cast<std::int64_t>(freeForCaller.QuadPart));
    }
    DWORD sectorsPerCluster, bytesPerSector, freeClusters, totalClusters;
    if (!gameapi::GetDiskFreeSpaceA(path, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters)) return 0.0;
    return x87::FromUnsigned(sectorsPerCluster) * x87::FromUnsigned(bytesPerSector) * x87::FromUnsigned(freeClusters);
}

bool QuitGame(int exitCode) {
    if (!g_quitAllowed) return false;
    gameapi::PostQuitMessage(exitCode);
    return true;
}

// The movie mode byte (1 unless the command line changed it; the loop passes it to the frame step).
std::uint8_t g_movieMode = /*00784752*/ 1;

// Called for every new object of the client (CPlayerClient::OnNewObject): the copy protection's check. It draws a
// number below 100 and, one time in a hundred, looked through a check list nothing fills; only the draw is left.
void DelayedProtectionCheck() { static_cast<void>(libcrt::RandBelow(100)); }

} // namespace app

CWorld* CEngineObject::World() const {
    auto* context = owner->context;
    return context ? context->ThisWorld() : nullptr;
}
