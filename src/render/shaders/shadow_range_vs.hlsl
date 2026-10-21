// The shadow map's depth range per cell (render_shadows.cpp; shadow_receiver.hlsli's `range`), two passes
// after the casters: shadow_reduce.hlsl, then shadow_dilate.hlsl, both with this triangle covering the target.
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
