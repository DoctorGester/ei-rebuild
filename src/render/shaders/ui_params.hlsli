// The interface's frame pictures (render_ui.cpp): the copy of the frame into a picture and the picture onto the frame.
// Each draws a rectangle (rect_vs.hlsli) and takes this block.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct UiParams {
    float4 colour;   // unused
    int4 source;     // ui_copy: left, top, width, height in the frame; ui_picture: z, w the picture's size
    int4 dest;       // ui_copy: the target's width, height; ui_picture: the destination's left, top
    float4 scale;    // ui_picture: source texels per destination pixel (x, y)
    int4 key;        // ui_picture: w greyed
};
ConstantBuffer<UiParams> ui : FS_BLOCK(0);

uint3 Bytes(float3 c) { return uint3(c * 255.0 + 0.5); }
float4 FromBytes(uint3 v) { return float4(float3(v) / 255.0, 1.0); }
#endif
