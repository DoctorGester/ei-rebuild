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
constexpr std::uintptr_t kNoMmxText = std::uintptr_t(0x0078E348);        // "MMX required in this version !!!.\n"
constexpr std::uintptr_t kLibraryLoadedText = std::uintptr_t(0x0078E444); // "MMX graphics release library loaded.\n"
constexpr std::uintptr_t kAcquiringText = std::uintptr_t(0x0078E3F4);     // "Acquiring graphics processor from graphics library.\n"
constexpr std::uintptr_t kNoLibraryText = std::uintptr_t(0x0078ED40);     // "CVisualizator::Create.\n Can't load 2D graphics library.\n"
constexpr std::uintptr_t kNoFunctionText = std::uintptr_t(0x0078ED7C);    // "... have no function \"GetGraphicsProcessor\".\n"


// 004F0B90 (RET 4): the transform pipeline's constructor (loads 3DfpFPU.dll from `folder`;
// render_helper.cpp), through its address: the verify build's wrapper runs there.
__declspec(naked) CTransformPipeline* __fastcall PipelineConstruct(void* /*memory*/, void*, const char* /*folder*/)
    EI_ORIGINAL(0x004F0B90)

} // namespace

void* CSceneRenderer::CreateBase(void* /*window*/, options::GameOptions* settings) {
    const mfc::CString& workPath = *reinterpret_cast<const mfc::CString*>(
        reinterpret_cast<const std::uint8_t*>(settings) + options::field::kWorkPath);

    void* memory = gamecrt::operator_new(kPipelineSize);
    CTransformPipeline* pipeline = memory ? PipelineConstruct(memory, nullptr, workPath) : nullptr;
    CTransformPipeline* old = pipeline_;
    pipeline_ = pipeline;
    if (pipeline) ++pipeline->refs_;
    if (old) old->Release();

    ReadProcessorInfo();
    if (!(TheProcessorInfo()->features & kProcessorMmx)) {
        game::FatalError(game::Text(kNoMmxText));
        ExitGame(-1);
    }

    graphicsLibrary_ = gameapi::LoadLibraryA(workPath + "2DintMMX.dll");   // the exe's 0078E46C
    game::DebugLog(game::Text(kLibraryLoadedText));
    if (!graphicsLibrary_) {
        game::FatalError(game::Text(kNoLibraryText));
        ExitGame(-1);
    }
    using GetGraphicsProcessor = void*(__cdecl*)();
    auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(static_cast<HMODULE>(graphicsLibrary_), "GetGraphicsProcessor"));   // the exe's 0078E42C
    if (!getProcessor) {
        game::FatalError(game::Text(kNoFunctionText));
        ExitGame(-1);
    }
    game::DebugLog(game::Text(kAcquiringText));
    graphicsProcessor_ = getProcessor();
    return graphicsProcessor_;
}
EI_REPLACE_METHOD(0x00500540, CSceneRenderer::CreateBase);
