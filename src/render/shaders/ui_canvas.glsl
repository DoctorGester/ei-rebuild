// The interface canvases (render_ui.cpp): a window surface's pixels kept in a render target as the surface's
// memory would hold them (8 bits a channel, each channel cut to the surface format's bits and widened again),
// drawn into by GDI-like commands and blitted onto the frame. Every program but ui_glyph draws a rectangle
// (rect.glsl). The block's unsigned values (bits, key) are ints here (sokol's uniform types).
@block ui_params
layout(binding = 1) uniform ui_params {
    vec4 colour;    // ui_solid: the colour (0..1); ui_compose: the text colour (0..255)
    ivec4 bits;     // the surface format's bits of red, green, blue; w: ui_grey's formula
    ivec4 source;   // ui_copy: left, top, width, height in the frame; ui_blit: left, top, texture width, height;
                    // ui_rgb565: z, w the canvas's width, height; ui_compose, ui_move: x, y taken off the pixel
    ivec4 dest;     // ui_copy, ui_rgb565: the target's width, height; ui_blit: the destination's left, top
    vec4 scale;     // ui_blit: source texels per destination pixel (x, y)
    ivec4 key;      // ui_blit: the colour key's red, green, blue (0..255); w: keyed
};
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;

uvec3 Bytes(vec3 c) { return uvec3(c * 255.0 + 0.5); }
vec4 FromBytes(uvec3 v) { return vec4(vec3(v) / 255.0, 1.0); }

// The top `n` bits of a channel widened back to 8 by repeating them (render_textures.cpp's Expand).
uint Widen(uint top, uint n) {
    if (n >= 8u) return top >> (n - 8u);
    if (n == 0u) return 0u;
    uint result = 0u;
    for (uint filled = 0u; filled < 8u; filled += n) result |= (top << (8u - n)) >> filled;
    return result;
}
uvec3 Widen3(uvec3 top) {
    return uvec3(Widen(top.r, uint(bits.r)), Widen(top.g, uint(bits.g)), Widen(top.b, uint(bits.b)));
}
uvec3 TopBits(uvec3 v) { return v >> (uvec3(8u) - uvec3(min(bits.rgb, ivec3(8)))); }
// An 8-bit colour as the surface stores it.
uvec3 Quantise(uvec3 v) { return Widen3(TopBits(v)); }
@end

// A glyph run moved to its row of the coverage target (rect.xy: its corner less the row's top left).
@vs ui_glyph_vs
@include_block rect_vertex
layout(location = 0) in vec4 position;   // xy: target pixels; zw: atlas texels, x < 0 an underline (full coverage)
layout(location = 0) out vec2 v_atlas;
void main() {
    gl_Position = to_clip(position.xy - rect.xy);
    v_atlas = position.zw;
}
@end

@fs ui_solid_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
void main() { frag_colour = colour; }
@end
@program ui_solid rect_vs ui_solid_fs

// A glyph run's coverage (an R8 target, blended with MAX: overlapping glyphs keep the larger).
@fs ui_glyph_fs
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
layout(location = 0) in vec2 v_atlas;
layout(location = 0) out vec4 frag_colour;
void main() {
    if (v_atlas.x < 0.0) {
        frag_colour = vec4(1.0, 0.0, 0.0, 0.0);
        return;
    }
    frag_colour = vec4(texelFetch(sampler2D(tex0, smp), ivec2(floor(v_atlas)), 0).r, 0.0, 0.0, 0.0);
}
@end
@program ui_glyph ui_glyph_vs ui_glyph_fs

// The run's colour over what the canvas held (tex1, a copy) by its coverage (tex0), rounded as
// render::text::ComposeAgainstBlack does over black. Both hold the run in its row (source.xy as in ui_glyph_vs).
@fs ui_compose_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
layout(binding = 1) uniform texture2D tex1;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy) - source.xy;
    uint cover = uint(texelFetch(sampler2D(tex0, smp), p, 0).r * 255.0 + 0.5);
    if (cover == 0u) discard;
    uvec3 under = Bytes(texelFetch(sampler2D(tex1, smp), p, 0).rgb);
    uvec3 text = uvec3(colour.rgb);
    frag_colour = FromBytes(Quantise((text * cover + under * (255u - cover) + 127u) / 255u));
}
@end
@program ui_compose rect_vs ui_compose_fs

// The greying on the surface format's own bits (tex0: a copy of the canvas).
@fs ui_grey_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
void main() {
    uvec3 v = Bytes(texelFetch(sampler2D(tex0, smp), ivec2(gl_FragCoord.xy), 0).rgb);
    if (bits.w == 0) {   // 5:6:5: (2 r + 2 g6 + b) / 16, green doubled
        uvec3 t = TopBits(v);
        uint grey = (2u * t.r + 2u * t.g + t.b) >> 4;
        frag_colour = FromBytes(Widen3(uvec3(grey, 2u * grey, grey)));
    } else if (bits.w == 1) {   // 5:5:5: (2 r + 4 g + b) / 16
        uvec3 t = TopBits(v);
        uint grey = (2u * t.r + 4u * t.g + t.b) >> 4;
        frag_colour = FromBytes(Widen3(uvec3(grey)));
    } else {   // 8:8:8 and the rest: on 8-bit channels
        uint grey = (2u * v.r + 4u * v.g + v.b) >> 4;
        frag_colour = FromBytes(Quantise(uvec3(grey)));
    }
}
@end
@program ui_grey rect_vs ui_grey_fs

// A rectangle of the frame (tex0) stretched over the canvas, nearest texel as CopyFrameTo picks it.
@fs ui_copy_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
void main() {
    uvec2 x = uvec2(gl_FragCoord.xy);   // the source rectangle and sizes are clamped non-negative
    uvec2 t = uvec2(source.xy) + (x * uvec2(source.zw)) / uvec2(dest.xy);
    frag_colour = FromBytes(Quantise(Bytes(texelFetch(sampler2D(tex0, smp), ivec2(t), 0).rgb)));
}
@end
@program ui_copy rect_vs ui_copy_fs

// Texels drawn as they are, each pixel the texel at its position less source.xy: a canvas's texels into a scratch
// target, an Image's pixels into a canvas.
@fs ui_move_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
void main() { frag_colour = texelFetch(sampler2D(tex0, smp), ivec2(gl_FragCoord.xy) - source.xy, 0); }
@end
@program ui_move rect_vs ui_move_fs

// A canvas (tex0) stretched over an R16_UINT target as 5:6:5 pixels, each channel's top bits: the save's
// picture. Each pixel takes the canvas pixel under its centre, (2 x + 1) * canvas size / (2 * target size).
@fs ui_rgb565_fs
@include_block ui_params
layout(location = 0) out uint frag_565;
void main() {
    uvec2 x = uvec2(gl_FragCoord.xy);
    uvec2 t = ((2u * x + 1u) * uvec2(source.zw)) / (2u * uvec2(dest.xy));
    uvec3 v = Bytes(texelFetch(sampler2D(tex0, smp), ivec2(t), 0).rgb);
    frag_565 = ((v.r >> 3) << 11) | ((v.g >> 2) << 5) | (v.b >> 3);
}
@end
@program ui_rgb565 rect_vs ui_rgb565_fs

// A canvas onto the frame as blit.glsl draws a window surface, the colour key compared on the stored colour.
@fs ui_blit_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
void main() {
    vec2 texel = vec2(source.xy) + floor((gl_FragCoord.xy - vec2(dest.xy)) * scale.xy);
    texel = clamp(texel, vec2(0.0), vec2(source.zw) - 1.0);
    vec4 c = texelFetch(sampler2D(tex0, smp), ivec2(texel), 0);
    if (key.w != 0 && all(equal(Bytes(c.rgb), uvec3(key.rgb)))) discard;
    frag_colour = vec4(c.rgb, 1.0);
}
@end
@program ui_blit rect_vs ui_blit_fs
