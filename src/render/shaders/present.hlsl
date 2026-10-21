// The present (render_window.cpp): the frame, with the software cursor over it, onto the back buffer through the
// game's gamma ramp. The cursor is not in the frame (the cursor thread draws the same frame with its cursor
// somewhere else): its texels replace the frame's where their alpha is a half or more, as the window blit
// (blit.hlsl) draws them, before the frame is scaled to the back buffer.
cbuffer params : register(b0) { float4 source; };
struct vs_out { float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    vs_out o;
    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    o.uv = lerp(source.xy, source.zw, p);
    return o;
}
cbuffer cursor_params : register(b0) {
    float4 cursorTo;      // left, top, right, bottom in frame pixels; right <= left: no cursor
    float4 frameInfo;     // x, y: frame width, height; z, w: cursor texels per frame pixel
    float4 cursorSize;    // x, y: the cursor texture's width, height
};
Texture2D<float4> tex : register(t0);
Texture2D<float4> ramp : register(t1);
Texture2D<float4> cursorTex : register(t2);
SamplerState smp : register(s0);
// The frame's pixel `p` with the cursor over it.
float3 Composed(int2 p) {
    p = clamp(p, int2(0, 0), int2(frameInfo.xy) - 1);
    float3 colour = tex.Load(int3(p, 0)).rgb;
    if (p.x >= cursorTo.x && p.x < cursorTo.z && p.y >= cursorTo.y && p.y < cursorTo.w) {
        int2 t = (int2)clamp(floor((float2(p) - cursorTo.xy) * frameInfo.zw), float2(0.0, 0.0), cursorSize.xy - 1.0);
        float4 c = cursorTex.Load(int3(t, 0));
        if (c.a >= 0.5) colour = c.rgb;
    }
    return colour;
}
float4 ps_main(vs_out i) : SV_Target0 {
    float3 colour;
    float2 q = i.uv * frameInfo.xy - 0.5;
    if (cursorTo.z > cursorTo.x && q.x > cursorTo.x - 2.0 && q.x < cursorTo.z && q.y > cursorTo.y - 2.0 && q.y < cursorTo.w) {
        // Near the cursor: the bilinear filter by hand over the pixels with the cursor in (what the sampler
        // gave the frame with the cursor drawn into it).
        int2 p0 = (int2)floor(q);
        float2 f = q - floor(q);
        colour = lerp(lerp(Composed(p0), Composed(p0 + int2(1, 0)), f.x),
                      lerp(Composed(p0 + int2(0, 1)), Composed(p0 + int2(1, 1)), f.x), f.y);
    } else {
        colour = tex.SampleLevel(smp, i.uv, 0).rgb;
    }
    int3 k = (int3)(saturate(colour) * 255.0 + 0.5);
    return float4(ramp.Load(int3(k.r, 0, 0)).r, ramp.Load(int3(k.g, 0, 0)).g, ramp.Load(int3(k.b, 0, 0)).b, 1.0);
}
