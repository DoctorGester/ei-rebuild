// The depth clear of a rectangle (render_blit.cpp): the depth only, at target.z.
cbuffer vs_params : register(b0) {
    float4 rect;     // left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height; z: the depth
};
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, target.z, 1.0);
}
float4 ps_main(float4 pos : SV_Position) : SV_Target0 { return float4(0.0, 0.0, 0.0, 0.0); }
