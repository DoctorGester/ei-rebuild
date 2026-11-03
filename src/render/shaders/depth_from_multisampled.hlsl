// The multisampling's copy of sample 0 of the multisampled depth into the frame target's depth (render_smooth.cpp),
// for any sample count (the count is not part of the texture's type). SDL_GPU cannot sample a multisampled texture,
// so only the shader check creates this program until the smooth option copies its depth another way.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
Texture2DMS<float> depth_ms : FS_TEXTURE(0);
SamplerState copy_smp : FS_SAMPLER(0);
struct PsOut {
    float4 colour : SV_Target0;
    float depth : SV_Depth;
};
PsOut ps_main(float4 pos : SV_Position) {
    PsOut o;
    o.depth = depth_ms.Load(int2(pos.xy), 0);
    o.colour = 0.0;
    return o;
}
#endif
