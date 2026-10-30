@vs texture_compose_vs
@glsl_options fixup_clipspace
@include_block rect_vertex
void main() { gl_Position = rect_position(); }
@end

@fs texture_compose_fs
layout(binding = 1) uniform texture_compose_params {
    ivec4 offset;   // x, y: the image texel drawn at target texel (0, 0); z: 1 to fill with `colour`; w: the alpha kept
                    // (render::BlankAlpha: 0 none, opaque; 1 only its top bit, a 1:5:5:5 texture's; 2 all)
    vec4 colour;
};
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    if (offset.z != 0) {
        frag_colour = colour;
        return;
    }
    vec4 c = texelFetch(sampler2D(tex0, smp), ivec2(gl_FragCoord.xy) + offset.xy, 0);
    if (offset.w == 0) c.a = 1.0;
    else if (offset.w == 1) c.a = c.a >= 0.5 ? 1.0 : 0.0;
    frag_colour = c;
}
@end
@program texture_compose texture_compose_vs texture_compose_fs
