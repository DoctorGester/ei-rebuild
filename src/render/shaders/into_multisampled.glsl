@vs into_multisampled_vs
@glsl_options fixup_clipspace
@include_block fullscreen_vertex
@end

@fs into_multisampled_fs
layout(binding = 0) uniform texture2D colour;
layout(binding = 1) uniform texture2D depth;
layout(binding = 0) uniform sampler copy_smp;
@image_sample_type depth unfilterable_float
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    gl_FragDepth = texelFetch(sampler2D(depth, copy_smp), p, 0).r;
    frag_colour = texelFetch(sampler2D(colour, copy_smp), p, 0);
}
@end
@program into_multisampled into_multisampled_vs into_multisampled_fs
