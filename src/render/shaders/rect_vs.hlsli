// The rectangle (rect.hlsli) as a program's vertex stage.
#include "rect.hlsli"
#ifdef EI_VERTEX
float4 vs_main(uint id : SV_VertexID) : SV_Position { return rect_position(id); }
#endif
