// A rectangle of the frame (tex0) stretched over a picture, nearest texel as CopyFrameTo picks it (ui_params.hlsli).
#include "ui_params.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint2 x = uint2(pos.xy);   // the source rectangle and sizes are clamped non-negative
    uint2 t = uint2(ui.sourceAt) + (x * uint2(ui.sourceSize)) / uint2(ui.targetSize);
    return FromBytes(Bytes(tex0.Load(int3(int2(t), 0)).rgb));
}
#endif
