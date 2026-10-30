// The present (render_window.cpp, and the cursor thread's in render_cursor.cpp): the frame, with the software
// cursor over it, onto the back buffer through the game's gamma ramp. The cursor is not in the frame (the cursor
// thread draws the same frame with its cursor somewhere else): its texels replace the frame's where their alpha is
// a half or more, as the image blit (blit.glsl) draws them, before the frame is scaled to the back buffer.
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
layout(binding = 1) uniform cursor_params {
    vec4 to;      // left, top, right, bottom in frame pixels; right <= left: no cursor
    vec4 frame;   // x, y: frame width, height; z, w: cursor texels per frame pixel
    vec4 size;    // x, y: the width and height of the cursor's part of its picture; z, w: its top left texel
} cursor;
layout(binding = 0) uniform texture2D tex;
layout(binding = 1) uniform texture2D ramp;
layout(binding = 2) uniform texture2D cursor_tex;
layout(binding = 0) uniform sampler smp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 frag_colour;
// The frame's pixel `p` with the cursor over it.
vec3 Composed(ivec2 p) {
    p = clamp(p, ivec2(0, 0), ivec2(cursor.frame.xy) - 1);
    vec3 colour = texelFetch(sampler2D(tex, smp), p, 0).rgb;
    if (p.x >= cursor.to.x && p.x < cursor.to.z && p.y >= cursor.to.y && p.y < cursor.to.w) {
        ivec2 t = ivec2(cursor.size.zw + clamp(floor((vec2(p) - cursor.to.xy) * cursor.frame.zw), vec2(0.0, 0.0),
                                               cursor.size.xy - 1.0));
        vec4 c = texelFetch(sampler2D(cursor_tex, smp), t, 0);
        if (c.a >= 0.5) colour = c.rgb;
    }
    return colour;
}
void main() {
    vec3 colour;
    vec2 q = v_uv * cursor.frame.xy - 0.5;
    if (cursor.to.z > cursor.to.x && q.x > cursor.to.x - 2.0 && q.x < cursor.to.z && q.y > cursor.to.y - 2.0 &&
        q.y < cursor.to.w) {
        // Near the cursor: the bilinear filter by hand over the pixels with the cursor in (what the sampler
        // gave the frame with the cursor drawn into it).
        ivec2 p0 = ivec2(floor(q));
        vec2 f = q - floor(q);
        colour = mix(mix(Composed(p0), Composed(p0 + ivec2(1, 0)), f.x),
                     mix(Composed(p0 + ivec2(0, 1)), Composed(p0 + ivec2(1, 1)), f.x), f.y);
    } else {
        colour = textureLod(sampler2D(tex, smp), v_uv, 0.0).rgb;
    }
    ivec3 k = ivec3(clamp(colour, 0.0, 1.0) * 255.0 + 0.5);
    frag_colour = vec4(texelFetch(sampler2D(ramp, smp), ivec2(k.r, 0), 0).r,
                       texelFetch(sampler2D(ramp, smp), ivec2(k.g, 0), 0).g,
                       texelFetch(sampler2D(ramp, smp), ivec2(k.b, 0), 0).b, 1.0);
}
@end
@program present present_vs present_fs
