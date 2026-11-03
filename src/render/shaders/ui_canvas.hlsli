// The interface canvases (render_ui.cpp): a window surface's pixels kept in a render target as the surface's memory
// would hold them (8 bits a channel, each channel cut to the surface format's bits and widened again), drawn into by
// GDI-like commands and blitted onto the frame. Every ui_ program but ui_glyph draws a rectangle (rect_vs.hlsli) and
// takes this block. Its unsigned values (bits, key) are ints here (as the C++ structs hold them).
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct UiParams {
    float4 colour;   // ui_solid: the colour (0..1); ui_compose: the text colour (0..255)
    int4 bits;       // the surface format's bits of red, green, blue; w: ui_grey's formula
    int4 source;     // ui_copy: left, top, width, height in the frame; ui_blit: left, top, texture width, height;
                     // ui_rgb565: z, w the canvas's width, height; ui_compose, ui_move: x, y taken off the pixel
    int4 dest;       // ui_copy, ui_rgb565: the target's width, height; ui_blit: the destination's left, top
    float4 scale;    // ui_blit: source texels per destination pixel (x, y)
    int4 key;        // ui_blit: the colour key's red, green, blue (0..255); w: keyed
};
ConstantBuffer<UiParams> ui : FS_BLOCK(0);

uint3 Bytes(float3 c) { return uint3(c * 255.0 + 0.5); }
float4 FromBytes(uint3 v) { return float4(float3(v) / 255.0, 1.0); }

// The top `n` bits of a channel widened back to 8 by repeating them (render_textures.cpp's Expand).
uint Widen(uint top, uint n) {
    if (n >= 8u) return top >> (n - 8u);
    if (n == 0u) return 0u;
    uint result = 0u;
    for (uint filled = 0u; filled < 8u; filled += n) result |= (top << (8u - n)) >> filled;
    return result;
}
uint3 Widen3(uint3 top) {
    return uint3(Widen(top.r, uint(ui.bits.r)), Widen(top.g, uint(ui.bits.g)), Widen(top.b, uint(ui.bits.b)));
}
uint3 TopBits(uint3 v) { return v >> (8u - uint3(min(ui.bits.rgb, 8))); }
// An 8-bit colour as the surface stores it.
uint3 Quantise(uint3 v) { return Widen3(TopBits(v)); }
#endif
