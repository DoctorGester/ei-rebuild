// The multisampling's copy of sample 0 of the multisampled depth into the frame target's depth
// (render_smooth.cpp), for any sample count (shader model 5: the count is not part of the texture's type).
@fs depth_from_multisampled_fs
layout(binding = 0) uniform texture2DMS depth_ms;
layout(binding = 0) uniform sampler copy_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    gl_FragDepth = texelFetch(sampler2DMS(depth_ms, copy_smp), ivec2(gl_FragCoord.xy), 0).r;
    frag_colour = vec4(0.0);
}
@end
@program depth_from_multisampled fullscreen_vs depth_from_multisampled_fs
