// The image blit (render_blit.cpp: the cursor, the movies, the fills): a rectangle of an image onto the frame,
// texel for pixel, cut out where the texel's alpha (the cursor's mask) is below a half.
@fs blit_fs
layout(binding = 1) uniform blit_params {
    vec4 dest;     // x, y: destination left, top; z, w: source texels per destination pixel
    vec4 source;   // x, y: source left, top; z, w: texture width, height
};
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    vec2 texel = source.xy + floor((gl_FragCoord.xy - dest.xy) * dest.zw);
    texel = clamp(texel, vec2(0.0), source.zw - 1.0);
    vec4 c = texelFetch(sampler2D(tex, smp), ivec2(texel), 0);
    if (c.a < 0.5) discard;   // the colour key
    frag_colour = vec4(c.rgb, 1.0);
}
@end
@program blit rect_vs blit_fs
