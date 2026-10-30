@vs ui_rect_vs
@glsl_options fixup_clipspace
@include_block rect_vertex
void main() { gl_Position = rect_position(); }
@end

@vs ui_glyph_vs
@glsl_options fixup_clipspace
@include_block rect_params
layout(location = 0) in vec4 position;
layout(location = 0) out vec2 v_atlas;
void main() {
    vec2 p = position.xy - rect.xy;
    gl_Position = vec4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
    v_atlas = position.zw;
}
@end

@block ui_params
layout(binding = 1) uniform ui_params {
    vec4 colour;    // ps_solid: the colour (0..1); ps_compose: the text colour (0..255)
    ivec4 bits;     // the surface format's bits of red, green, blue; w: ps_grey's formula
    ivec4 source;   // ps_copy: left, top, width, height in the frame; ps_blit: left, top, texture width, height;
                    // ps_rgb565: z, w the canvas's width, height; ps_compose: x, y taken off the pixel
    ivec4 dest;     // ps_copy, ps_rgb565: the target's width, height; ps_blit: the destination's left, top
    vec4 scale;     // ps_blit: source texels per destination pixel (x, y)
    ivec4 key;      // ps_blit: the colour key's red, green, blue (0..255); w: keyed
};
@end

@block ui_quantise
uvec3 Bytes(vec3 c) { return uvec3((c * 255.0 + 0.5)); }
vec4 FromBytes(uvec3 v) { return vec4(vec3(v) / 255.0, 1.0); }

// The top `n` bits of a channel widened back to 8 by repeating them (render_textures.cpp's Expand).
uint Widen(uint top, uint n) {
    if (n >= 8) return top >> (n - 8);
    if (n == 0) return 0;
    uint result = 0;
    for (uint filled = 0; filled < 8; filled += n) result |= (top << (8 - n)) >> filled;
    return result;
}
uvec3 Widen3(uvec3 top) {
    return uvec3(Widen(top.r, uint(bits.r)), Widen(top.g, uint(bits.g)), Widen(top.b, uint(bits.b)));
}
uvec3 TopBits(uvec3 v) { return v >> (uvec3(8) - uvec3(min(bits.rgb, ivec3(8)))); }
// An 8-bit colour as the surface stores it.
uvec3 Quantise(uvec3 v) { return Widen3(TopBits(v)); }
@end

@fs ui_solid_fs
@include_block ui_params
layout(location = 0) out vec4 frag_colour;
void main() { frag_colour = colour; }
@end
@program ui_solid ui_rect_vs ui_solid_fs

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

@fs ui_compose_fs
@include_block ui_params
layout(binding = 0) uniform texture2D tex0;
layout(binding = 1) uniform texture2D tex1;
layout(binding = 0) uniform sampler smp;
@include_block ui_quantise
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy) - source.xy;
    uint cover = uint((texelFetch(sampler2D(tex0, smp), p, 0).r * 255.0 + 0.5));
    if (cover == 0) discard;
    uvec3 under = Bytes(texelFetch(sampler2D(tex1, smp), p, 0).rgb);
    uvec3 text = uvec3(colour.rgb);
    frag_colour = FromBytes(Quantise((text * cover + under * (255 - cover) + 127) / 255));
}
@end
@program ui_compose ui_rect_vs ui_compose_fs

@fs ui_grey_fs
@include_block ui_params
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
@include_block ui_quantise
layout(location = 0) out vec4 frag_colour;
void main() {
    uvec3 v = Bytes(texelFetch(sampler2D(tex0, smp), ivec2(gl_FragCoord.xy), 0).rgb);
    if (bits.w == 0) {   // 5:6:5: (2 r + 2 g6 + b) / 16, green doubled
        uvec3 t = TopBits(v);
        uint grey = (2 * t.r + 2 * t.g + t.b) >> 4;
        {
            frag_colour = FromBytes(Widen3(uvec3(grey, 2 * grey, grey)));
            return;
        }
    }
    if (bits.w == 1) {   // 5:5:5: (2 r + 4 g + b) / 16
        uvec3 t = TopBits(v);
        uint grey = (2 * t.r + 4 * t.g + t.b) >> 4;
        {
            frag_colour = FromBytes(Widen3(uvec3(grey, grey, grey)));
            return;
        }
    }
    uint grey = (2 * v.r + 4 * v.g + v.b) >> 4;   // 8:8:8 and the rest: on 8-bit channels
    frag_colour = FromBytes(Quantise(uvec3(grey, grey, grey)));
}
@end
@program ui_grey ui_rect_vs ui_grey_fs

@fs ui_copy_fs
@include_block ui_params
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
@include_block ui_quantise
layout(location = 0) out vec4 frag_colour;
void main() {
    uvec2 x = uvec2(gl_FragCoord.xy);   // the source rectangle and sizes are clamped non-negative
    uvec2 t = uvec2(source.xy) + (x * uvec2(source.zw)) / uvec2(dest.xy);
    frag_colour = FromBytes(Quantise(Bytes(texelFetch(sampler2D(tex0, smp), ivec2(t), 0).rgb)));
}
@end
@program ui_copy ui_rect_vs ui_copy_fs

@fs ui_rgb565_fs
@include_block ui_params
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
@include_block ui_quantise
layout(location = 0) out uint frag_colour;
void main() {
    uvec2 x = uvec2(gl_FragCoord.xy);
    uvec2 t = ((2 * x + 1) * uvec2(source.zw)) / (2 * uvec2(dest.xy));
    uvec3 v = Bytes(texelFetch(sampler2D(tex0, smp), ivec2(t), 0).rgb);
    frag_colour = ((v.r >> 3) << 11) | ((v.g >> 2) << 5) | (v.b >> 3);
}
@end
@program ui_rgb565 ui_rect_vs ui_rgb565_fs

@fs ui_blit_fs
@include_block ui_params
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
@include_block ui_quantise
layout(location = 0) out vec4 frag_colour;
void main() {
    vec2 texel = vec2(source.xy) + floor((gl_FragCoord.xy - vec2(dest.xy)) * scale.xy);
    texel = clamp(texel, vec2(0.0, 0.0), vec2(source.zw) - 1.0);
    vec4 c = texelFetch(sampler2D(tex0, smp), ivec2(texel), 0);
    if (key.w != 0 && all(equal(Bytes(c.rgb), uvec3(key.rgb)))) discard;
    frag_colour = vec4(c.rgb, 1.0);
}
@end
@program ui_blit ui_rect_vs ui_blit_fs
