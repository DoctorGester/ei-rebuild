// The present (render_window.cpp): the frame onto the back buffer through the game's gamma ramp.
cbuffer params : register(b0) { float4 source; };
struct vs_out { float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    vs_out o;
    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    o.uv = lerp(source.xy, source.zw, p);
    return o;
}
Texture2D<float4> tex : register(t0);
Texture2D<float4> ramp : register(t1);
SamplerState smp : register(s0);
float4 ps_main(vs_out i) : SV_Target0 {
    int3 k = (int3)(saturate(tex.Sample(smp, i.uv).rgb) * 255.0 + 0.5);
    return float4(ramp.Load(int3(k.r, 0, 0)).r, ramp.Load(int3(k.g, 0, 0)).g, ramp.Load(int3(k.b, 0, 0)).b, 1.0);
}
