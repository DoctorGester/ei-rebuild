// SDL_GPU's register spaces (docs/notes/renderer.md, "HLSL and SDL_shadercross"), included by every program: in the
// vertex stage textures and samplers in space0, uniform blocks in space1; in the fragment stage textures and samplers
// in space2, uniform blocks in space3. A texture and its sampler share a slot (SDL binds them as a pair). The slots
// of a stage run from 0 without gaps (tools/gen_gpu_shaders.py checks).
#ifndef EI_BINDINGS_HLSLI
#define EI_BINDINGS_HLSLI
#define EI_CAT(a, b) a##b
#define VS_TEXTURE(n) register(EI_CAT(t, n), space0)
#define VS_SAMPLER(n) register(EI_CAT(s, n), space0)
#define VS_BLOCK(n) register(EI_CAT(b, n), space1)
#define FS_TEXTURE(n) register(EI_CAT(t, n), space2)
#define FS_SAMPLER(n) register(EI_CAT(s, n), space2)
#define FS_BLOCK(n) register(EI_CAT(b, n), space3)
#endif
