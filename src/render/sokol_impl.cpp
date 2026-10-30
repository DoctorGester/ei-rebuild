// The sokol_gfx implementation, selecting the host's backend (third_party/sokol, unmodified).
#define SOKOL_IMPL
#if defined(_WIN32)
#define SOKOL_D3D11
#elif defined(__APPLE__)
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif
#if defined(EI_RENDER_VALIDATION) || defined(EI_NATIVE_VALIDATION)
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "sokol_gfx.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(__APPLE__)
// This sokol revision has no public command-buffer query. Empty commits rotate its uniform-buffer slots
// without a GPU fence, so native blits must only flush when a render/compute command buffer exists.
bool EiMetalHasPendingCommands() { return _sg.mtl.cmd_buffer != nil; }
#endif
