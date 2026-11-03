// One triangle covering the target (three vertices, no buffer): the vertex stage of the passes whose fragment stage
// reads its source at its own pixel (into_multisampled, depth_from_multisampled, shadow_reduce, shadow_dilate).
#include "bindings.hlsli"
#ifdef EI_VERTEX
float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
#endif
