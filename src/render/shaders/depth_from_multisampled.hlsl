// The smooth option's copy of sample 0 of the multisampled depth into the frame target's depth
// (render_smooth.cpp). SAMPLES: the sample count (shader model 4 wants the multisampled texture's count
// in its type); compiled once for each count the device can give (CMakeLists.txt).
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
Texture2DMS<float, SAMPLES> depth : register(t0);
float4 ps_main(float4 pos : SV_Position, out float z : SV_Depth) : SV_Target0 {
    z = depth.Load(int2(pos.xy), 0);
    return float4(0.0, 0.0, 0.0, 0.0);
}
