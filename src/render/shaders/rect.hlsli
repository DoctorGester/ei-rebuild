// The rectangle programs' vertex stage (blit, perf_overlay, texture_compose, texture_mip, texture_clouds, the ui_
// programs): two triangles (six vertices, no buffer) covering a rectangle of the target. render_internal.h's
// ApplyRect sets the block. rect_vs.hlsli is the stage itself; ui_glyph takes only `to_clip`.
#include "bindings.hlsli"
#ifdef EI_VERTEX
struct RectParams {
    float4 rect;     // left, top, right, bottom (target pixels)
    float4 target;   // x, y: the target's width, height
};
ConstantBuffer<RectParams> rect_params : VS_BLOCK(0);
// `p` (target pixels) in clip space.
float4 to_clip(float2 p) {
    return float4(p.x * 2.0 / rect_params.target.x - 1.0, 1.0 - p.y * 2.0 / rect_params.target.y, 0.0, 1.0);
}
float4 rect_position(uint id) {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    return to_clip(lerp(rect_params.rect.xy, rect_params.rect.zw, corner));
}
#endif
