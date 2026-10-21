// The renderer base part's creation (00500540, CVisualizator::Create in the original's messages;
// base slot 2 of vtable 0073D8A0, called once by the renderer's creation 00500B70; A-U12): makes
// the transform pipeline, checks the processor for MMX and loads the 2D graphics library.
#include "model.h"
#include "game_strings.h"

#include "options.h"
#include "sys_info.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"

namespace {

constexpr std::size_t kPipelineSize = 0x94;
constexpr const char* kNoMmxText = "MMX required in this version !!!.\n";
constexpr const char* kLibraryLoadedText = "MMX graphics release library loaded.\n";
constexpr const char* kAcquiringText = "Acquiring graphics processor from graphics library.\n";
constexpr const char* kNoLibraryText = "CVisualizator::Create.\n Can't load 2D graphics library.\n";
constexpr const char* kNoFunctionText = "CVisualizator::Create.\n Loaded 2D graphics library have no function \"GetGraphicsProcessor\".\n";


} // namespace

void* CSceneRenderer::CreateBase(void* /*window*/, options::GameOptions* settings) {
    const mfc::CString& workPath = settings->workPath_;

    void* memory = gamecrt::operator_new(kPipelineSize);
    CTransformPipeline* pipeline = memory ? static_cast<CTransformPipeline*>(memory)->TransformPipelineConstruct(workPath) : nullptr;
    CTransformPipeline* old = pipeline_;
    pipeline_ = pipeline;
    if (pipeline) ++pipeline->refs_;
    if (old) old->Release();

    ReadProcessorInfo();
    if (!(TheProcessorInfo()->features & kProcessorMmx)) {
        game::FatalError(kNoMmxText);
        libcrt::Exit(-1);
    }

    graphicsLibrary_ = gameapi::LoadLibraryA(workPath + "2DintMMX.dll");   // the exe's 0078E46C
    game::DebugLog(kLibraryLoadedText);
    if (!graphicsLibrary_) {
        game::FatalError(kNoLibraryText);
        libcrt::Exit(-1);
    }
    using GetGraphicsProcessor = void*(__cdecl*)();
    auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(static_cast<HMODULE>(graphicsLibrary_), "GetGraphicsProcessor"));   // the exe's 0078E42C
    if (!getProcessor) {
        game::FatalError(kNoFunctionText);
        libcrt::Exit(-1);
    }
    game::DebugLog(kAcquiringText);
    graphicsProcessor_ = getProcessor();
    return graphicsProcessor_;
}
EI_REPLACE_METHOD(0x00500540, CSceneRenderer::CreateBase);
