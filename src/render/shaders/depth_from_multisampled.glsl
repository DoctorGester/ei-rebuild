@vs depth_from_multisampled_vs
@glsl_options fixup_clipspace
@include_block fullscreen_vertex
@end

@fs depth_from_multisampled_fs
layout(binding = 0) uniform texture2DMS depth_ms;
layout(binding = 0) uniform sampler copy_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    gl_FragDepth = texelFetch(sampler2DMS(depth_ms, copy_smp), ivec2(gl_FragCoord.xy), 0).r;
    frag_colour = vec4(0.0);
}
@end
@program depth_from_multisampled depth_from_multisampled_vs depth_from_multisampled_fs
