// Texels drawn as they are, each pixel the texel at its position less source.xy: a canvas's texels into a scratch
// target, an Image's pixels into a canvas (ui_canvas.hlsli).
#include "ui_canvas.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 { return tex0.Load(int3(int2(pos.xy) - ui.source.xy, 0)); }
#endif
