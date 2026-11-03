// The multisampling's copy into the multisampled targets (render_smooth.cpp): every sample of a pixel takes the
// source's colour and depth at that pixel, and the depth copy that depth.
@fs into_multisampled_fs
layout(binding = 0) uniform texture2D colour;
layout(binding = 1) uniform texture2D depth;
layout(binding = 0) uniform sampler copy_smp;
@image_sample_type depth unfilterable_float
layout(location = 0) out vec4 frag_colour;
layout(location = 1) out float frag_depth;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float d = texelFetch(sampler2D(depth, copy_smp), p, 0).r;
    gl_FragDepth = d;
    frag_depth = d;
    frag_colour = texelFetch(sampler2D(colour, copy_smp), p, 0);
}
@end
@program into_multisampled fullscreen_vs into_multisampled_fs
