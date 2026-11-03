// A glyph run's coverage (an R8 target, blended with MAX: overlapping glyphs keep the larger) for the interface
// canvases (ui_canvas.hlsli): the run moved to its row of the coverage target (rect.xy: its corner less the row's top
// left).
#include "rect.hlsli"
struct VsIn {
    [[vk::location(0)]] float4 position : TEXCOORD0;   // xy: target pixels; zw: atlas texels, x < 0 an underline
};
struct VsOut {
    [[vk::location(0)]] float2 atlas : TEXCOORD0;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
VsOut vs_main(VsIn i) {
    VsOut o;
    o.pos = to_clip(i.position.xy - rect_params.rect.xy);
    o.atlas = i.position.zw;
    return o;
}
#endif
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(VsOut i) : SV_Target0 {
    if (i.atlas.x < 0.0) return float4(1.0, 0.0, 0.0, 0.0);   // an underline: full coverage
    return float4(tex0.Load(int3(int2(floor(i.atlas)), 0)).r, 0.0, 0.0, 0.0);
}
#endif
