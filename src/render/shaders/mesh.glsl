// The meshes (render_mesh.cpp): the fixed-function texture stages of the old device as the game sets them
// (D3DTEXTUREOP and D3DTA values), evaluated per pixel; then specular, fog and the alpha test. A figure
// part lit on the GPU (render::MeshLighting) takes its diffuse colour from `figure_light` instead of
// the vertex colour (whose alpha it keeps).
@vs mesh_vs
layout(binding = 0) uniform mesh_vs_params {
    mat4 matrix;
    vec4 distanceFog;    // x: on, y: start, z: 1 / (end - start); w: keep z inside the depth range
    vec4 shift;          // xy: the half-pixel shift (render_internal.h); z: depth scale
    mat4 light;          // model space to the shadow map
    vec4 uvOffset;       // xy: added to the texture coordinates (a mesh kept on the GPU)
    vec4 normalOffset;   // x: how far along its normal a receiver's point is looked up in the map
    vec4 landscape[3];   // model space to the landscape's: rows (point shadows)
};
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_col;
layout(location = 2) in vec4 in_spec;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec3 in_normal;
layout(location = 0) out vec4 v_col;
layout(location = 1) out vec4 v_spec;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_shadow;
layout(location = 4) out vec3 v_model;
layout(location = 5) out vec3 v_normal;
layout(location = 6) out vec3 v_land;
void main() {
    vec4 p = vec4(in_pos, 1.0);
    gl_Position = vec4(dot(matrix[0], p), dot(matrix[1], p), dot(matrix[2], p), dot(matrix[3], p));
    // Meshes the old path projects to the screen without clipping (the interface's) are not cut at
    // the near and far planes there either.
    if (distanceFog.w > 0.5) gl_Position.z = clamp(gl_Position.z, 0.0, gl_Position.w);
    gl_Position.z *= shift.z;   // the flat geometry's depth scale (shadows drawn just in front)
    gl_Position.xy += shift.xy * gl_Position.w;
    v_col = in_col.zyxw;
    v_spec = in_spec.zyxw;
    // The old path's vertex fog by distance (3DfpFPU slot 35): the fog factor in the specular
    // alpha falls from the start of the range, the smaller of it and the vertex's own is kept.
    if (distanceFog.x > 0.5 && gl_Position.w >= distanceFog.y)
        v_spec.a = min(v_spec.a, 1.0 - min((gl_Position.w - distanceFog.y) * distanceFog.z, 1.0));
    v_uv = in_uv + uvOffset.xy;
    // The normal offset: the point looked up in the map moved along its normal (a figure part's).
    float len = length(in_normal);
    vec4 q = len > 1e-6 ? vec4(in_pos + in_normal * (normalOffset.x / len), 1.0) : p;
    v_shadow = vec4(dot(light[0], q), dot(light[1], q), dot(light[2], q), 1.0);
    v_model = in_pos;
    v_normal = in_normal;
    v_land = vec3(dot(landscape[0], p), dot(landscape[1], p), dot(landscape[2], p));
}
@end

@fs mesh_fs
layout(binding = 1) uniform mesh_fs_params {
    vec4 fogColour;      // rgb
    vec4 factor;         // TEXTUREFACTOR
    ivec4 colourOps;     // stage 0: op, arg1, arg2; w: stage 1 op
    ivec4 colourOps1;    // stage 1: arg1, arg2
    ivec4 alphaOps;      // stage 0: op, arg1, arg2; w: stage 1 op
    ivec4 alphaOps1;     // stage 1: arg1, arg2
    ivec4 switches;      // x: specular, y: fog, z: alpha test func (0 off), w: alpha reference
    vec4 patchTo;        // stage 0's patch (render::TexturePatch): its rectangle (u0, v0, u1, v1), zero: none
    vec4 patchMap;       // the patch texture's coordinates: uv * xy + zw
    vec4 patchInside;    // ... kept inside its rectangle (u0, v0, u1, v1)
    vec4 shadowFigure;   // x: a unit's mesh, y: its object's nearest depth in the map (MeshBatch::front)
    vec4 pointOn;        // x: point shadows taken (the landscape rows set), y: a unit's mesh
    vec4 pointFront;     // per map: a unit's nearest distance to the light (MeshBatch::pointFront)
};
// A figure part lit on the GPU (render_lighting.cpp, MeshLightUniforms): the sun and lights in the part's model space.
layout(binding = 3) uniform mesh_light_params {
    vec4 on;               // x: lit here, y: the crown's pull, z: the lights
    vec4 sun;              // xyz: towards the sun in model space
    vec4 sunColour;        // rgb: the sun's lit colour
    vec4 ambient;          // rgb
    vec4 emissive;         // rgb
    vec4 pointAt[8];       // per light: model-space position xyz, w: 1 / range squared
    vec4 pointColour[8];   // per light: lit colour rgb, w: its point shadow's map + 1, 0 none
} lighting;
@include_block shadow_receiver
@include_block point_shadow
layout(location = 0) in vec4 v_col;
layout(location = 1) in vec4 v_spec;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_shadow;
layout(location = 4) in vec3 v_model;
layout(location = 5) in vec3 v_normal;
layout(location = 6) in vec3 v_land;
layout(binding = 0) uniform texture2D tex0;
layout(binding = 1) uniform texture2D tex1;
layout(binding = 2) uniform texture2D tex2;   // stage 0's patch
layout(binding = 0) uniform sampler smp0;
layout(binding = 1) uniform sampler smp1;
layout(binding = 2) uniform sampler smp2;
// A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting). See docs/notes/renderer.md,
// "The renderer's source files".
vec3 figure_light(vec3 p, vec3 n, vec3 land, vec3 face, vec2 turn) {
    vec3 ambient = lighting.ambient.rgb;
    float facing = dot(lighting.sun.xyz, n);
    vec3 lit = max(ambient, lighting.sunColour.rgb * facing);
    int count = int(lighting.on.z);
    for (int k = 0; k < count; ++k) {
        vec4 at = lighting.pointAt[k], colour = lighting.pointColour[k];
        vec3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float c = dot(d * inversesqrt(max(distance2, 1e-12)), n);
        float seen = 1.0;
        if (colour.w > 0.5 && pointOn.x > 0.5 && c > 0.0) {
            int slot = int(colour.w) - 1;
            if (pointOn.y > 0.5) {
                vec4 fronts = pointFront;   // (sokol-shdc takes no variable component of a uniform)
                seen = point_seen(slot, land, vec3(0.0), fronts[slot], turn);
            } else {
                float facing = clamp(dot(face, normalize(pointShadow.lights[slot].xyz - land)) * 6.0, 0.0, 1.0);
                if (facing > 0.0) seen = mix(1.0, point_seen(slot, land, face, kNoPointFront, turn), facing);
            }
        }
        lit = max(lit, colour.rgb * c * falloff * seen);
    }
    vec3 c = min(vec3(1.0), lit + lighting.emissive.rgb);
    if (lighting.on.y > 0.0) {
        vec3 s = lighting.sunColour.rgb * length(lighting.sun.xyz);
        vec3 t = clamp(ambient / max(s, 1e-6), -1.0, 1.0);
        vec3 mean = 0.5 * (ambient * (t + 1.0) + s * (1.0 - t * t) * 0.5);
        c = mix(c, min(vec3(1.0), mean + lighting.emissive.rgb), lighting.on.y);
    }
    return c;
}
vec4 arg(int a, vec4 current, vec4 diffuse, vec4 texel, vec4 spec) {
    int sel = a & 15;
    vec4 v = sel == 0 ? diffuse : sel == 2 ? texel : sel == 3 ? factor : sel == 4 ? spec : current;
    if ((a & 16) != 0) v = 1.0 - v;
    if ((a & 32) != 0) v = v.aaaa;
    return v;
}
vec4 combine(int op, vec4 a1, vec4 a2, vec4 current, vec4 diffuse, vec4 texel) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 5) return a1 * a2 * 2.0;
    if (op == 6) return a1 * a2 * 4.0;
    if (op == 7) return a1 + a2;
    if (op == 8) return a1 + a2 - 0.5;
    if (op == 9) return (a1 + a2 - 0.5) * 2.0;
    if (op == 10) return a1 - a2;
    if (op == 11) return a1 + a2 - a1 * a2;
    if (op == 12) return mix(a2, a1, diffuse.a);
    if (op == 13) return mix(a2, a1, texel.a);
    if (op == 14) return mix(a2, a1, factor.a);
    if (op == 15) return a1 + a2 * (1.0 - texel.a);
    if (op == 16) return mix(a2, a1, current.a);
    if (op == 18) return vec4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return vec4(a1.rgb * a2.rgb, a1.a + a2.a);
    if (op == 20) return vec4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);
    if (op == 24) return vec4(dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0));
    return a1 * a2;   // 4 MODULATE and anything not handled
}
bool passes(int func, float a, int reference) {
    int v = int(roundEven(clamp(a, 0.0, 1.0) * 255.0));
    if (func == 1) return false;
    if (func == 2) return v < reference;
    if (func == 3) return v == reference;
    if (func == 4) return v <= reference;
    if (func == 5) return v > reference;
    if (func == 6) return v != reference;
    if (func == 7) return v >= reference;
    return true;
}
// A texture stage: its colour and alpha operations (op, arg1, arg2) on `texel`, clamped.
vec4 stage(ivec3 colourOp, ivec3 alphaOp, vec4 current, vec4 diffuse, vec4 texel) {
    vec3 c = combine(colourOp.x, arg(colourOp.y, current, diffuse, texel, v_spec),
                     arg(colourOp.z, current, diffuse, texel, v_spec), current, diffuse, texel).rgb;
    float a = current.a;
    if (alphaOp.x > 1)
        a = combine(alphaOp.x, arg(alphaOp.y, current, diffuse, texel, v_spec),
                    arg(alphaOp.z, current, diffuse, texel, v_spec), current, diffuse, texel).a;
    return clamp(vec4(c, a), 0.0, 1.0);
}
layout(location = 0) out vec4 frag_colour;
void main() {
    vec3 dsx = dFdx(v_shadow.xyz), dsy = dFdy(v_shadow.xyz);
    // The face normal in the landscape's space, turned to the camera (point shadows).
    vec3 face = cross(dFdx(v_land), dFdy(v_land));
    float face_length = length(face);
    face = face_length > 1e-20 ? face / face_length : vec3(0.0, 0.0, 1.0);
    if (dot(face, pointShadow.eye.xyz - v_land) < 0.0) face = -face;
    // How much of the sun the shadow map hides. A unit's mesh: the shadows other objects cast, on
    // every face (none of its own: its sun side is shaded by the lighting's N.L; by face it flipped triangle
    // by triangle on a low-poly creature). Any other mesh: on faces turned to the sun only, by their face
    // normal (the smoothed vertex normals of many static meshes lean far from their faces: a plank's top read
    // sideways). Every mesh is darkened by it alike, the colour lit here or by the game, the lamps' light and
    // the ambient light with the sun's (as before: in the towns the ambient light is nearly the sun's).
    float hidden = 0.0;
    if (shadow.on.x > 0.5) {
        bool unit = shadowFigure.x > 0.5;
        hidden = shadow_amount(v_shadow.xyz, dsx, dsy, gl_FragCoord.xy, unit ? shadowFigure.y : 2.0);
        if (!unit) hidden *= shadow_facing(v_shadow.xyz, dsx, dsy);
    }
    vec4 diffuse = v_col;
    if (lighting.on.x > 0.5) diffuse.rgb = figure_light(v_model, v_normal, v_land, face, pixel_turn(gl_FragCoord.xy));
    vec4 current = diffuse;
    vec4 t0 = texture(sampler2D(tex0, smp0), v_uv);
    // Stage 0's patch: inside its rectangle the texels come from the patch texture (a unit's face).
    vec2 w = v_uv - floor(v_uv);
    vec4 tp = texture(sampler2D(tex2, smp2), clamp(w * patchMap.xy + patchMap.zw, patchInside.xy, patchInside.zw));
    if (all(greaterThanEqual(w, patchTo.xy)) && all(lessThan(w, patchTo.zw))) t0 = tp;
    if (colourOps.x > 1) {
        current = stage(colourOps.xyz, alphaOps.xyz, current, diffuse, t0);
        if (colourOps.w > 1) {
            vec4 t1 = texture(sampler2D(tex1, smp1), v_uv);
            current = stage(ivec3(colourOps.w, colourOps1.xy), ivec3(alphaOps.w, alphaOps1.xy), current, diffuse, t1);
        }
    }
    if (switches.z != 0 && !passes(switches.z, current.a, switches.w)) discard;
    vec3 rgb = current.rgb;
    if (switches.x != 0) rgb = clamp(rgb + v_spec.rgb, 0.0, 1.0);
    if (shadow.on.x > 0.5) {
        if (shadow.on.z > 0.5) {   // the harness's shadowview
            frag_colour = vec4(vec3(1.0 - hidden), current.a);
            return;
        }
        rgb *= 1.0 - hidden * shadow.on.y;
    }
    if (switches.y != 0) rgb = mix(fogColour.rgb, rgb, v_spec.a);
    frag_colour = vec4(rgb, current.a);
}
@end
@program mesh mesh_vs mesh_fs
