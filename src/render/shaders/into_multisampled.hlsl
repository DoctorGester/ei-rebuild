// The smooth option's copy into the multisampled targets (render_smooth.cpp): a triangle covering the
// target; the pixel shader reads the source at its own pixel.
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
Texture2D<float4> colour : register(t0);
Texture2D<float> depth : register(t1);
float4 ps_main(float4 pos : SV_Position, out float z : SV_Depth) : SV_Target0 {
    int3 at = int3(pos.xy, 0);
    z = depth.Load(at);
    return colour.Load(at);
}
