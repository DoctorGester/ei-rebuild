// The multisampling's copy of the resolved depth copy (each pixel's samples' mean) into the frame target's depth
// (render_smooth.cpp).
@fs depth_from_resolved_fs
layout(binding = 0) uniform texture2D depth_copy;
layout(binding = 0) uniform sampler copy_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    gl_FragDepth = texelFetch(sampler2D(depth_copy, copy_smp), ivec2(gl_FragCoord.xy), 0).r;
    frag_colour = vec4(0.0);
}
@end
@program depth_from_resolved fullscreen_vs depth_from_resolved_fs
