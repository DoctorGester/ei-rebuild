// The performance overlay (render_perf.cpp): its text image (one channel: 0 the box, else the text's
// brightness) scaled up by whole pixels over the back buffer, the box translucent black.
@fs perf_overlay_fs
layout(binding = 1) uniform perf_overlay_params {
    vec4 origin;   // x, y: the box's top left corner (pixels); z: screen pixels per image pixel
};
layout(binding = 0) uniform texture2D tex0;
layout(binding = 0) uniform sampler smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 texel = ivec2(floor((gl_FragCoord.xy - origin.xy) / origin.z));
    float v = texelFetch(sampler2D(tex0, smp), texel, 0).r;
    frag_colour = v > 0.0 ? vec4(v, v, v, 1.0) : vec4(0.0, 0.0, 0.0, 0.6);
}
@end
@program perf_overlay rect_vs perf_overlay_fs
