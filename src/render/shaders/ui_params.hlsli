// The interface's frame pictures (render_ui.cpp): the copy of the frame into a picture and the picture onto the frame.
// Each draws a rectangle (rect_vs.hlsli) and takes this block.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct UiParams {
    int2 sourceAt;     // ui_copy: the rectangle's left, top in the frame
    int2 sourceSize;   // ui_copy: its width, height; ui_picture: the picture's
    int2 targetSize;   // ui_copy: the target's
    int2 destAt;       // ui_picture: the destination's left, top
    float2 scale;      // ui_picture: source texels per destination pixel
    int grey;          // ui_picture
};
ConstantBuffer<UiParams> ui : FS_BLOCK(0);

uint3 Bytes(float3 c) { return uint3(c * 255.0 + 0.5); }
float4 FromBytes(uint3 v) { return float4(float3(v) / 255.0, 1.0); }
#endif
