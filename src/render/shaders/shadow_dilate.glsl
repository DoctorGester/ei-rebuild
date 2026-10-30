@vs shadow_dilate_vs
@glsl_options fixup_clipspace
@include_block fullscreen_vertex
@end

@fs shadow_dilate_fs
layout(binding = 0) uniform shadow_dilate_params { ivec4 reach; };
layout(binding = 0) uniform texture2D cells;
layout(binding = 0) uniform sampler point_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    int last = reach.y;
    float lo = 1.0, hi = 0.0;
    for (int j = -reach.x; j <= reach.x; ++j) {
        for (int i = -reach.x; i <= reach.x; ++i) {
            vec4 r = texelFetch(sampler2D(cells, point_smp), ivec2(clamp(c + ivec2(i, j), 0, last)), 0);
            lo = min(lo, r.x);
            hi = max(hi, r.y);
        }
    }
    frag_colour = vec4(lo, hi, texelFetch(sampler2D(cells, point_smp), ivec2(c), 0).x, 0.0);
}
@end
@program shadow_dilate shadow_dilate_vs shadow_dilate_fs
