// The renderer base part's creation (CVisualizator::Create in the game's messages;
// called once by the renderer's creation): makes
// the transform pipeline, checks the processor for MMX and loads the 2D graphics library.
#include "model.h"
#include "game_strings.h"

#include "options.h"
#include "sys_info.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

namespace {

constexpr const char* kNoMmxText = "MMX required in this version !!!.\n";
constexpr const char* kLibraryLoadedText = "MMX graphics release library loaded.\n";
constexpr const char* kAcquiringText = "Acquiring graphics processor from graphics library.\n";
constexpr const char* kNoLibraryText = "CVisualizator::Create.\n Can't load 2D graphics library.\n";
constexpr const char* kNoFunctionText = "CVisualizator::Create.\n Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n";


} // namespace

void* CVisualizator::CreateBase(void* /*window*/, options::GameOptions* settings) {
    const mfc::CString& workPath = settings->workPath_;

    CTransformPipeline* pipeline = new CTransformPipeline(workPath);   // 0x94 bytes
    CTransformPipeline* old = pipeline_;
    pipeline_ = pipeline;
    if (pipeline) ++pipeline->refs_;
    if (old) old->Release();

    ReadProcessorInfo();
    if (!(TheProcessorInfo()->features & kProcessorMmx)) {
        game::FatalError(kNoMmxText);
        libcrt::Exit(-1);
    }

    graphicsLibrary_ = gameapi::LoadLibraryA(workPath + "2DintMMX.dll");
    game::DebugLog(kLibraryLoadedText);
    if (!graphicsLibrary_) {
        game::FatalError(kNoLibraryText);
        libcrt::Exit(-1);
    }
    using GetGraphicsProcessor = CPixelConverter*(__cdecl*)();
    auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(static_cast<HMODULE>(graphicsLibrary_), "GetGraphicsProcessor"));
    if (!getProcessor) {
        game::FatalError(kNoFunctionText);
        libcrt::Exit(-1);
    }
    game::DebugLog(kAcquiringText);
    graphicsProcessor_ = getProcessor();
    return graphicsProcessor_;
}
EI_REWRITTEN(0x00500540, CVisualizator::CreateBase);
