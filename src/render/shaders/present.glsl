// The present (render_window.cpp): the frame onto the back buffer through the game's gamma ramp.
@vs present_vs
layout(binding = 0) uniform present_params {
    vec4 source;   // the frame's part shown: left, top, right, bottom (0..1)
};
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    v_uv = mix(source.xy, source.zw, p);
}
@end

@fs present_fs
layout(binding = 0) uniform texture2D tex;
layout(binding = 1) uniform texture2D ramp;
layout(binding = 0) uniform sampler smp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_colour;
void main() {
    vec3 colour = textureLod(sampler2D(tex, smp), v_uv, 0.0).rgb;
    ivec3 k = ivec3(clamp(colour, 0.0, 1.0) * 255.0 + 0.5);
    frag_colour = vec4(texelFetch(sampler2D(ramp, smp), ivec2(k.r, 0), 0).r,
                       texelFetch(sampler2D(ramp, smp), ivec2(k.g, 0), 0).g,
                       texelFetch(sampler2D(ramp, smp), ivec2(k.b, 0), 0).b, 1.0);
}
@end
@program present present_vs present_fs
