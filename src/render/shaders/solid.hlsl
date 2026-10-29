// A solid rectangle (render_blit.cpp): the depth clear in the middle of the frame (its pipeline writes the depth
// only, at target.z) and the movie player's fill (its pipeline writes the colour only).
cbuffer vs_params : register(b0) {
    float4 rect;     // left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height; z: the depth
    float4 colour;   // the fill's colour
};
struct Corner {
    float4 pos : SV_Position;
    nointerpolation float4 colour : COLOR0;
};
Corner vs_main(uint id : SV_VertexID) {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    Corner o;
    o.pos = float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, target.z, 1.0);
    o.colour = colour;
    return o;
}
float4 ps_main(Corner c) : SV_Target0 { return c.colour; }
