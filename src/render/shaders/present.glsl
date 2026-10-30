@vs present_vs
@glsl_options fixup_clipspace
layout(binding = 0) uniform present_params { vec4 source; };
layout(location = 0) out vec2 v_uv;
void main() {
    uint id = uint(gl_VertexIndex);
    vec2 p = vec2((id << 1) & 2, id & 2);
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    v_uv = mix(source.xy, source.zw, p);
}
@end

@fs present_fs
layout(binding = 1) uniform cursor_params {
    vec4 cursorTo;     // left, top, right, bottom in frame pixels; right <= left: no cursor
    vec4 frameInfo;    // x, y: frame width, height; z, w: cursor texels per frame pixel
    vec4 cursorSize;   // x, y: the width and height of the cursor's part of its picture; z, w: its top left texel
};
layout(binding = 0) uniform texture2D tex;
layout(binding = 1) uniform texture2D ramp;
layout(binding = 2) uniform texture2D cursorTex;
layout(binding = 0) uniform sampler smp;
layout(location = 0) in vec2 v_uv;
vec3 Composed(ivec2 p) {
    p = clamp(p, ivec2(0, 0), ivec2(frameInfo.xy) - 1);
    vec3 colour = texelFetch(sampler2D(tex, smp), p, 0).rgb;
    if (p.x >= cursorTo.x && p.x < cursorTo.z && p.y >= cursorTo.y && p.y < cursorTo.w) {
        ivec2 t = ivec2((cursorSize.zw +
                         clamp(floor((vec2(p) - cursorTo.xy) * frameInfo.zw), vec2(0.0, 0.0), cursorSize.xy - 1.0)));
        vec4 c = texelFetch(sampler2D(cursorTex, smp), t, 0);
        if (c.a >= 0.5) colour = c.rgb;
    }
    return colour;
}
layout(location = 0) out vec4 frag_colour;
void main() {
    vec3 colour;
    vec2 q = v_uv * frameInfo.xy - 0.5;
    if (cursorTo.z > cursorTo.x && q.x > cursorTo.x - 2.0 && q.x < cursorTo.z && q.y > cursorTo.y - 2.0 &&
        q.y < cursorTo.w) {
        // Near the cursor: the bilinear filter by hand over the pixels with the cursor in (what the sampler
        // gave the frame with the cursor drawn into it).
        ivec2 p0 = ivec2(floor(q));
        vec2 f = q - floor(q);
        colour = mix(mix(Composed(p0), Composed(p0 + ivec2(1, 0)), f.x),
                     mix(Composed(p0 + ivec2(0, 1)), Composed(p0 + ivec2(1, 1)), f.x), f.y);
    } else {
        colour = textureLod(sampler2D(tex, smp), v_uv, 0).rgb;
    }
    ivec3 k = ivec3((clamp(colour, 0.0, 1.0) * 255.0 + 0.5));
    frag_colour =
        vec4(texelFetch(sampler2D(ramp, smp), ivec2(k.r, 0), 0).r, texelFetch(sampler2D(ramp, smp), ivec2(k.g, 0), 0).g,
             texelFetch(sampler2D(ramp, smp), ivec2(k.b, 0), 0).b, 1.0);
}
@end
@program present present_vs present_fs
