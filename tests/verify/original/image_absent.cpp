// The image side of the harness (tests/verify/image/image_side.h) in ei_harness.dll, which runs only in game.exe:
// none. The shared harness calls those functions only in the image's runs (ei::Standalone()), where they are linked in
// with the game (docs/notes/standalone.md section 5); one reached here is a bug of the shared harness, reported in the
// log and ending the run. Each function of image_side.h and of the digest's image_side (harness_digest_read.h) has its
// body here: a new one is a link error of ei_harness.dll until it has one.
#include "../harness_digest_read.h"
#include "../image/image_side.h"
#include "../../../src/hook/log.h"

#include <windows.h>

#include <string>
#include <vector>

namespace {

[[noreturn]] void Absent(const char* function) {
    ei::Log("FAILED: the harness called the image's %s in game.exe", function);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) Sleep(INFINITE);
}

} // namespace

namespace ei::verify::image {
void ObserveDraws(DrawWriter) { Absent(__func__); }
void* TopWindow() { Absent(__func__); }
std::uintptr_t WindowKind(const void*) { Absent(__func__); }
bool BriefingPlayerList(void*, PlayerList*) { Absent(__func__); }
void* ClientUnit(unsigned, std::uint32_t**, std::uint32_t**) { Absent(__func__); }
std::vector<std::string> NetState() { Absent(__func__); }
const char* WindowTitle() { Absent(__func__); }
void* MainWindow() { Absent(__func__); }
std::uint8_t& InterfaceHasScreen() { Absent(__func__); }
std::uint8_t& RecordingFrames() { Absent(__func__); }
bool AppActive() { Absent(__func__); }
int& FlowState() { Absent(__func__); }
void SetLoadTarget(const char*) { Absent(__func__); }
void ClearShowTutorial() { Absent(__func__); }
void SetMasterHost(const char*) { Absent(__func__); }
void SetWaterWaves(bool) { Absent(__func__); }
CursorState Cursor() { Absent(__func__); }
void* QueuedWindow() { Absent(__func__); }
bool CloseAllPending() { Absent(__func__); }
bool ScreenFade(bool*, float*) { Absent(__func__); }
int CampaignMode() { Absent(__func__); }
void* ClientWorldObject() { Absent(__func__); }
void* SceneDevice() { Absent(__func__); }
void* DeviceLock() { Absent(__func__); }
void ConsoleExecute(const char*) { Absent(__func__); }
void SendLeave04(int) { Absent(__func__); }
void SetConstructionVariable(std::uint32_t) { Absent(__func__); }
void ObserveConsoleLines(void (*)(const char* line)) { Absent(__func__); }
void ObserveDirectInput(void (*)(IDirectInput7A* input)) { Absent(__func__); }
unsigned& UpdatePeriod() { Absent(__func__); }
const char* DataFolder() { Absent(__func__); }
void SetFixedSeed(void (*)(std::uint32_t* results)) { Absent(__func__); }
int TextKey(const char*) { Absent(__func__); }
void* LocalWorldPlayer(int) { Absent(__func__); }
void* WorldHandle() { Absent(__func__); }
const char* ServerZoneName() { Absent(__func__); }
void* PickAt2(void*, int, int) { Absent(__func__); }
void* UnitAt(void*, int, int) { Absent(__func__); }
int ScreenRect(void*, int, RECT*) { Absent(__func__); }
int ObjectId(const void*) { Absent(__func__); }
std::uint32_t ObjectFlags(const void*) { Absent(__func__); }
void* PlayerConnection(void*) { Absent(__func__); }
int ConnectionState(void*) { Absent(__func__); }
bool CampItems(void*, int*, std::vector<void*>*, std::vector<unsigned>*) { Absent(__func__); }
void CampTakeItem(void*, void*, int) { Absent(__func__); }
void PickSurface(void*, float*, int, int) { Absent(__func__); }
void LoadUnitFaces(void*) { Absent(__func__); }
void BlinkOpen(void*) { Absent(__func__); }
void BlinkClose(void*) { Absent(__func__); }
bool UnitFaceHalves(std::vector<void*>*) { Absent(__func__); }
int RegionCount(void*) { Absent(__func__); }
bool Region(void*, int, RegionInfo*) { Absent(__func__); }
void ScaleToScreen(long*, long*) { Absent(__func__); }
void InterfaceSize(int*, int*) { Absent(__func__); }
bool SaveLoadSliding(void*) { Absent(__func__); }
bool OptionsSliding(void*) { Absent(__func__); }
bool HasChatOverlay() { Absent(__func__); }
void ChatKeyEnter() { Absent(__func__); }
void CloseChat() { Absent(__func__); }
bool HasClassTemplate(int) { Absent(__func__); }
const void* FindMap(const char*) { Absent(__func__); }
int MapKind(const void*) { Absent(__func__); }
void LeaveToZone(void*, const char*, int) { Absent(__func__); }
bool RestoreSurfaces(long*) { Absent(__func__); }
std::string HudState(void*, const std::string&) { Absent(__func__); }
int ServerUpdate(void*) { Absent(__func__); }
int ClientSegment(void*) { Absent(__func__); }
void CameraUpdate(void*, float) { Absent(__func__); }
void FrameStep(bool, bool) { Absent(__func__); }
void CursorLock(void*) { Absent(__func__); }
CursorPresents CursorPresentCounts() { Absent(__func__); }
WindowPresents WindowPresentCounts() { Absent(__func__); }
bool ReadCursorPresent(int*, int*, std::vector<std::uint8_t>*) { Absent(__func__); }
bool FrameInWindow(int*, int*) { Absent(__func__); }
void FrameToWindow(int*, int*) { Absent(__func__); }
void WindowToFrame(int*, int*) { Absent(__func__); }
void ConfigureRenderer(const char*) { Absent(__func__); }
void SetWaitForVsync(bool) { Absent(__func__); }
void LoseDevice() { Absent(__func__); }
bool RemoveDevice() { Absent(__func__); }
void EndFrame() { Absent(__func__); }
bool ReadFrame(int*, int*, std::vector<std::uint8_t>*) { Absent(__func__); }
bool RenderStep(const std::string&, const std::string&, std::vector<std::string>*, std::vector<std::string>*) { Absent(__func__); }
const char* TextFolder() { Absent(__func__); }
long ModFile(const char*, const char*) { Absent(__func__); }
void ObserveStreamWrites(void (*)(void* stream, std::uint32_t size, void* const* frame)) { Absent(__func__); }
const char* SettingsFileName() { Absent(__func__); }
std::string SettingsText(const std::vector<SettingsValue>&) { Absent(__func__); }
void PinSettings(const char*, SettingsMissing, bool) { Absent(__func__); }
void DefaultSettings(const char*, int, int) { Absent(__func__); }
void AddSettingsReadFilter(SettingsReadFilter) { Absent(__func__); }
int& GamePort() { Absent(__func__); }
CameraState Camera(const void*) { Absent(__func__); }
namespace utf8files {
std::string Check(const char*) { Absent(__func__); }

} // namespace utf8files
namespace utf8typing {
std::string Type(HWND, const std::string&) { Absent(__func__); }
std::string CheckConsoleLine(const std::string&) { Absent(__func__); }

} // namespace utf8typing
namespace utf8text {
std::string Check() { Absent(__func__); }
std::string CheckChatWrap(const std::string&) { Absent(__func__); }

} // namespace utf8text

} // namespace ei::verify::image

namespace ei::verify::digest::image_side {
void Read(void*, Snapshot&) { Absent(__func__); }
UnitNow Unit(void*, int) { Absent(__func__); }
int LocalLeader(void*, unsigned*) { Absent(__func__); }
void NoteAddresses(void*) { Absent(__func__); }
} // namespace ei::verify::digest::image_side
