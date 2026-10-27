// The performance overlay (render_perf.cpp): its text image (one channel: 0 the box, else the text's
// brightness) scaled up by whole pixels over the back buffer, the box translucent black.
cbuffer vs_params : register(b0) {
    float4 rect;     // left, top, right, bottom (back buffer pixels)
    float4 target;   // x, y: the back buffer's width, height
};
cbuffer fs_params : register(b0) {
    float4 origin;   // x, y: the box's top left corner (pixels); z: screen pixels per image pixel
};

Texture2D<float4> tex0 : register(t0);

float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}

float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 texel = int2(floor((pos.xy - origin.xy) / origin.z));
    float v = tex0.Load(int3(texel, 0)).r;
    if (v > 0.0) return float4(v, v, v, 1.0);
    return float4(0.0, 0.0, 0.0, 0.6);
}
