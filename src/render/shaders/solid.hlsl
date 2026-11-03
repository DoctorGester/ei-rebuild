// A solid rectangle (render_blit.cpp): the depth clear in the middle of the frame (its pipeline writes the depth only,
// at target.z), the movie player's fill (its pipeline writes the colour only) and a point shadow face's clear
// (render_point_shadows.cpp).
#include "bindings.hlsli"
struct VsOut {
    [[vk::location(0)]] nointerpolation float4 colour : TEXCOORD0;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
struct SolidParams {
    float4 rect;     // left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height; z: the depth
    float4 colour;   // the fill's colour
};
ConstantBuffer<SolidParams> params : VS_BLOCK(0);
VsOut vs_main(uint id : SV_VertexID) {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(params.rect.xy, params.rect.zw, corner);
    VsOut o;
    o.pos = float4(p.x * 2.0 / params.target.x - 1.0, 1.0 - p.y * 2.0 / params.target.y, params.target.z, 1.0);
    o.colour = params.colour;
    return o;
}
#endif
#ifdef EI_FRAGMENT
float4 ps_main(VsOut i) : SV_Target0 { return i.colour; }
#endif
