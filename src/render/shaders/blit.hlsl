// The window blit (render_blit.cpp): a rectangle of a window's texture onto the frame, texel for pixel,
// cut out where the texel's alpha (the colour key) is below a half.
cbuffer vs_params : register(b0) {
    float4 rect;     // destination left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height
};
cbuffer fs_params : register(b0) {
    float4 dest;     // x, y: destination left, top; z, w: source texels per destination pixel
    float4 source;   // x, y: source left, top; z, w: texture width, height
};
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}
Texture2D<float4> tex : register(t0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 texel = source.xy + floor((pos.xy - dest.xy) * dest.zw);
    texel = clamp(texel, float2(0.0, 0.0), source.zw - 1.0);
    float4 c = tex.Load(int3(texel, 0));
    if (c.a < 0.5) discard;   // the colour key
    return float4(c.rgb, 1.0);
}
