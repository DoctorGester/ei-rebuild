// Image rectangles, texel for pixel, cut out where the cursor mask is below a half.
@block rect_params
layout(binding = 0) uniform rect_params {
    vec4 rect;
    vec4 target;
};
@end

@block rect_vertex
@include_block rect_params
vec4 rect_position() {
    int id = gl_VertexIndex;
    vec2 corner = vec2(id == 1 || id == 2 || id == 4, id == 2 || id == 4 || id == 5);
    vec2 p = mix(rect.xy, rect.zw, corner);
    return vec4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}
@end

@vs blit_vs
@glsl_options fixup_clipspace
@include_block rect_vertex
void main() { gl_Position = rect_position(); }
@end

@fs blit_fs
layout(binding = 1) uniform blit_params {
    vec4 dest;
    vec4 source;
};
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    vec2 texel = source.xy + floor((gl_FragCoord.xy - dest.xy) * dest.zw);
    texel = clamp(texel, vec2(0.0), source.zw - 1.0);
    vec4 c = texelFetch(sampler2D(tex, smp), ivec2(texel), 0);
    if (c.a < 0.5) discard;
    frag_colour = vec4(c.rgb, 1.0);
}
@end
@program blit blit_vs blit_fs
