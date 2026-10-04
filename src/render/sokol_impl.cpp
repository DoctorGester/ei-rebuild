// The sokol_gfx implementation for the new renderer, with the Direct3D 11 backend
// (third_party/sokol, unmodified). Its own warnings are not ours to fix.
#define SOKOL_IMPL
#define SOKOL_D3D11
#ifdef EI_RENDER_VALIDATION
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#pragma warning(push, 0)
#include "sokol_gfx.h"
#pragma warning(pop)
