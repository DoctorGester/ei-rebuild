@vs perf_overlay_vs
@glsl_options fixup_clipspace
@include_block rect_vertex
void main() { gl_Position = rect_position(); }
@end

@fs perf_overlay_fs
layout(binding = 1) uniform perf_overlay_params {
    vec4 origin;   // x, y: the box's top left corner (pixels); z: screen pixels per image pixel
};
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 texel = ivec2(floor((gl_FragCoord.xy - origin.xy) / origin.z));
    float v = texelFetch(sampler2D(tex0, smp), ivec2(texel), 0).r;
    if (v > 0.0) {
        frag_colour = vec4(v, v, v, 1.0);
        return;
    }
    frag_colour = vec4(0.0, 0.0, 0.0, 0.6);
}
@end
@program perf_overlay perf_overlay_vs perf_overlay_fs
