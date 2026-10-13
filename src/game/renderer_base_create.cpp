// The renderer base part's creation (00500540, CVisualizator::Create in the original's messages;
// base slot 2 of vtable 0073D8A0, called once by the renderer's creation 00500B70; A-U12): makes
// the transform pipeline, checks the processor for MMX and loads the 2D graphics library.
#include "model.h"

#include "options.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <intrin.h>

namespace {

constexpr std::size_t kPipelineSize = 0x94;
constexpr std::uint32_t kEflagsId = 0x00200000;   // EFLAGS.ID: can be flipped when CPUID exists
constexpr std::uint32_t kFeatureMmx = 0x00800000; // CPUID.1:EDX bit 23

// What CPUID function 1 reported (the globals keep their zeros on a processor without CPUID):
// EAX, EBX, ECX as twelve bytes, a terminator, then EDX (the feature flags).
struct ProcessorInfo {
    std::uint32_t signature;   // 007CAED0
    std::uint32_t brand;       // 007CAED4
    std::uint32_t features2;   // 007CAED8
    std::uint8_t terminator;   // 007CAEDC
    std::uint8_t unknown_0D[3];
    std::uint32_t features;    // 007CAEE0
};
constexpr std::uintptr_t kProcessorInfo = std::uintptr_t(0x007CAED0);

constexpr std::uintptr_t kNoMmxText = std::uintptr_t(0x0078E348);        // "MMX required in this version !!!.\n"
constexpr std::uintptr_t kLibraryLoadedText = std::uintptr_t(0x0078E444); // "MMX graphics release library loaded.\n"
constexpr std::uintptr_t kAcquiringText = std::uintptr_t(0x0078E3F4);     // "Acquiring graphics processor from graphics library.\n"
constexpr std::uintptr_t kNoLibraryText = std::uintptr_t(0x0078ED40);     // "CVisualizator::Create.\n Can't load 2D graphics library.\n"
constexpr std::uintptr_t kNoFunctionText = std::uintptr_t(0x0078ED7C);    // "... have no function \"GetGraphicsProcessor\".\n"

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// 004F0B90 (RET 4): the transform pipeline's constructor (loads 3DfpFPU.dll from `folder`).
__declspec(naked) CTransformPipeline* __fastcall PipelineConstruct(void* /*memory*/, void*, const char* /*folder*/)
    EI_ORIGINAL(0x004F0B90)

// Whether the processor has CPUID: the ID flag flips. As in the original it stays flipped.
bool HasCpuid() {
    const std::uint32_t before = static_cast<std::uint32_t>(__readeflags());
    __writeeflags(before ^ kEflagsId);
    return (static_cast<std::uint32_t>(__readeflags()) ^ before) != 0;
}

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

    auto* processor = reinterpret_cast<ProcessorInfo*>(kProcessorInfo);
    processor->terminator = 0;
    if (HasCpuid()) {
        int registers[4];
        __cpuid(registers, 1);
        processor->features = static_cast<std::uint32_t>(registers[3]);
        processor->signature = static_cast<std::uint32_t>(registers[0]);
        processor->brand = static_cast<std::uint32_t>(registers[1]);
        processor->features2 = static_cast<std::uint32_t>(registers[2]);
    }
    // (The original copies the twelve bytes as a string into a local nothing reads.)
    if (!(processor->features & kFeatureMmx)) {
        FatalErrorOriginal(Text(kNoMmxText));
        ExitGame(-1);
    }

    graphicsLibrary_ = gameapi::LoadLibraryA(workPath + "2DintMMX.dll");   // the exe's 0078E46C
    DebugMessageOriginal(Text(kLibraryLoadedText));
    if (!graphicsLibrary_) {
        FatalErrorOriginal(Text(kNoLibraryText));
        ExitGame(-1);
    }
    using GetGraphicsProcessor = void*(__cdecl*)();
    auto getProcessor = reinterpret_cast<GetGraphicsProcessor>(
        gameapi::GetProcAddress(static_cast<HMODULE>(graphicsLibrary_), "GetGraphicsProcessor"));   // the exe's 0078E42C
    if (!getProcessor) {
        FatalErrorOriginal(Text(kNoFunctionText));
        ExitGame(-1);
    }
    DebugMessageOriginal(Text(kAcquiringText));
    graphicsProcessor_ = getProcessor();
    return graphicsProcessor_;
}
EI_REPLACE_METHOD(0x00500540, CSceneRenderer::CreateBase);
