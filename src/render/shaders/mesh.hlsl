// The meshes (render_mesh.cpp): the fixed-function texture stages of the old device as the game sets them
// (D3DTEXTUREOP and D3DTA values), evaluated per pixel; then specular, fog and the alpha test. A figure part lit on
// the GPU (render::MeshLighting) takes its diffuse colour from `figure_light` instead of the vertex colour (whose
// alpha it keeps).
#include "bindings.hlsli"
#include "depth_copy.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
};
struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float4 spec : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 model : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float3 land : TEXCOORD6;
    float4 pos : SV_Position;
};

#ifdef EI_VERTEX
struct MeshVsParams {
    row_major float4x4 matrix;
    float4 distanceFog;         // x: on, y: start, z: 1 / (end - start); w: keep z inside the depth range
    float4 shift;               // xy: the half-pixel shift (render_internal.h); z: depth scale
    row_major float4x4 light;   // model space to the shadow map
    float4 uvOffset;            // xy: added to the texture coordinates (a mesh kept on the GPU)
    float4 normalOffset;        // x: how far along its normal a receiver's point is looked up in the map
    float4 landscape[3];        // model space to the landscape's: rows (point shadows)
};
ConstantBuffer<MeshVsParams> vs : VS_BLOCK(0);
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    // Meshes the old path projects to the screen without clipping (the interface's) are not cut at the near and far
    // planes there either.
    if (vs.distanceFog.w > 0.5) o.pos.z = clamp(o.pos.z, 0.0, o.pos.w);
    o.pos.z *= vs.shift.z;   // the flat geometry's depth scale (shadows drawn just in front)
    o.pos.xy += vs.shift.xy * o.pos.w;
    o.col = i.col.zyxw;
    o.spec = i.spec.zyxw;
    // The old path's vertex fog by distance (3DfpFPU slot 35): the fog factor in the specular alpha falls from the
    // start of the range, the smaller of it and the vertex's own is kept.
    if (vs.distanceFog.x > 0.5 && o.pos.w >= vs.distanceFog.y)
        o.spec.a = min(o.spec.a, 1.0 - min((o.pos.w - vs.distanceFog.y) * vs.distanceFog.z, 1.0));
    o.uv = i.uv + vs.uvOffset.xy;
    // The normal offset: the point looked up in the map moved along its normal (a figure part's).
    float len = length(i.normal);
    float4 q = len > 1e-6 ? float4(i.pos + i.normal * (vs.normalOffset.x / len), 1.0) : p;
    o.shadow = float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
    o.model = i.pos;
    o.normal = i.normal;
    o.land = float3(dot(vs.landscape[0], p), dot(vs.landscape[1], p), dot(vs.landscape[2], p));
    return o;
}
#endif

#ifdef EI_FRAGMENT
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 3
#define SHADOW_RANGE_SLOT 4
#define POINT_BLOCK_SLOT 3
#define POINT_MAP_SLOT 5
#define CLOUD_MAP_SLOT 6
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
struct MeshFsParams {
    float4 fogColour;      // rgb
    float4 factor;         // TEXTUREFACTOR
    int4 colourOps;        // stage 0: op, arg1, arg2; w: stage 1 op
    int4 colourOps1;       // stage 1: arg1, arg2
    int4 alphaOps;         // stage 0: op, arg1, arg2; w: stage 1 op
    int4 alphaOps1;        // stage 1: arg1, arg2
    int4 switches;         // x: specular, y: fog, z: alpha test func (0 off), w: alpha reference
    float4 patchTo;        // stage 0's patch (render::TexturePatch): its rectangle (u0, v0, u1, v1), zero: none
    float4 patchMap;       // the patch texture's coordinates: uv * xy + zw
    float4 patchInside;    // ... kept inside its rectangle (u0, v0, u1, v1)
    float4 shadowFigure;   // x: a unit's mesh, y: its object's nearest depth in the map (MeshBatch::front)
    float4 pointOn;        // x: point shadows taken (the landscape rows set), y: a unit's mesh
    float4 pointFront;     // per map: a unit's nearest distance to the light (MeshBatch::pointFront)
};
ConstantBuffer<MeshFsParams> fs : FS_BLOCK(0);
// A figure part lit on the GPU (render_lighting.cpp, MeshLightUniforms): the sun and lights in the part's model space.
struct MeshLightParams {
    float4 on;               // x: lit here, z: the lights
    float4 sun;              // xyz: towards the sun in model space
    float4 sunColour;        // rgb: the sun's lit colour
    float4 ambient;          // rgb
    float4 emissive;         // rgb
    float4 pointAt[8];       // per light: model-space position xyz, w: 1 / range squared
    float4 pointColour[8];   // per light: lit colour rgb, w: its point shadow's map + 1, 0 none
};
ConstantBuffer<MeshLightParams> lighting : FS_BLOCK(2);
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp0 : FS_SAMPLER(0);
Texture2D<float4> tex1 : FS_TEXTURE(1);
SamplerState smp1 : FS_SAMPLER(1);
Texture2D<float4> tex2 : FS_TEXTURE(2);   // stage 0's patch
SamplerState smp2 : FS_SAMPLER(2);

// A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting). See docs/notes/renderer.md, "The
// renderer's source files".
float3 figure_light(float3 p, float3 n, float3 land, float3 face, float2 turn) {
    float3 ambient = lighting.ambient.rgb;
    float facing = dot(lighting.sun.xyz, n);
    float3 lit = max(ambient, lighting.sunColour.rgb * facing);
    int count = (int)lighting.on.z;
    for (int k = 0; k < count; ++k) {
        float4 at = lighting.pointAt[k], colour = lighting.pointColour[k];
        float3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float c = dot(d * rsqrt(max(distance2, 1e-12)), n);
        float seen = 1.0;
        if (colour.w > 0.5 && fs.pointOn.x > 0.5 && c > 0.0) {
            int slot = (int)colour.w - 1;
            if (fs.pointOn.y > 0.5) {
                float4 fronts = fs.pointFront;
                seen = point_seen(slot, land, 0.0, fronts[slot], turn);
            } else {
                float lightFacing = saturate(dot(face, normalize(pointShadow.lights[slot].xyz - land)) * 6.0);
                if (lightFacing > 0.0) seen = lerp(1.0, point_seen(slot, land, face, kNoPointFront, turn), lightFacing);
            }
        }
        lit = max(lit, colour.rgb * c * falloff * seen);
    }
    float3 c = min(1.0, lit + lighting.emissive.rgb);
    return c;
}
float4 arg(int a, float4 current, float4 diffuse, float4 texel, float4 spec) {
    int sel = a & 15;
    float4 v = sel == 0 ? diffuse : sel == 2 ? texel : sel == 3 ? fs.factor : sel == 4 ? spec : current;
    if ((a & 16) != 0) v = 1.0 - v;
    if ((a & 32) != 0) v = v.aaaa;
    return v;
}
float4 combine(int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 texel) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 5) return a1 * a2 * 2.0;
    if (op == 6) return a1 * a2 * 4.0;
    if (op == 7) return a1 + a2;
    if (op == 8) return a1 + a2 - 0.5;
    if (op == 9) return (a1 + a2 - 0.5) * 2.0;
    if (op == 10) return a1 - a2;
    if (op == 11) return a1 + a2 - a1 * a2;
    if (op == 12) return lerp(a2, a1, diffuse.a);
    if (op == 13) return lerp(a2, a1, texel.a);
    if (op == 14) return lerp(a2, a1, fs.factor.a);
    if (op == 15) return a1 + a2 * (1.0 - texel.a);
    if (op == 16) return lerp(a2, a1, current.a);
    if (op == 18) return float4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return float4(a1.rgb * a2.rgb, a1.a + a2.a);
    if (op == 20) return float4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);
    if (op == 24) return dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0).xxxx;
    return a1 * a2;   // 4 MODULATE and anything not handled
}
// HLSL's round takes halves to the even neighbour, as the GLSL's roundEven.
bool passes(int func, float a, int reference) {
    int v = (int)round(saturate(a) * 255.0);
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
float4 stage(int3 colourOp, int3 alphaOp, float4 current, float4 diffuse, float4 texel, float4 spec) {
    float3 c = combine(colourOp.x, arg(colourOp.y, current, diffuse, texel, spec),
                       arg(colourOp.z, current, diffuse, texel, spec), current, diffuse, texel).rgb;
    float a = current.a;
    if (alphaOp.x > 1)
        a = combine(alphaOp.x, arg(alphaOp.y, current, diffuse, texel, spec),
                    arg(alphaOp.z, current, diffuse, texel, spec), current, diffuse, texel).a;
    return saturate(float4(c, a));
}

float4 Shade(VsOut i) {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    // The face normal in the landscape's space, turned to the camera (point shadows).
    float3 face = cross(ddx(i.land), ddy(i.land));
    float faceLength = length(face);
    face = faceLength > 1e-20 ? face / faceLength : float3(0.0, 0.0, 1.0);
    if (dot(face, pointShadow.eye.xyz - i.land) < 0.0) face = -face;
    // How much of the sun the shadow map hides. A unit's mesh: the shadows other objects cast, on every face (none of
    // its own: its sun side is shaded by the lighting's N.L; by face it flipped triangle by triangle on a low-poly
    // creature). Any other mesh: on faces turned to the sun only, by their face normal (the smoothed vertex normals of
    // many static meshes lean far from their faces: a plank's top read sideways). The clouds' alike. Every mesh is
    // darkened by it alike, the colour lit here or by the game, the lamps' light and the ambient light with the sun's
    // (as before: in the towns the ambient light is nearly the sun's).
    bool unit = fs.shadowFigure.x > 0.5;
    float hidden = with_clouds(shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, unit ? fs.shadowFigure.y : 2.0),
                               i.shadow.xy);
    if (!unit && hidden > 0.0) hidden *= shadow_facing(i.shadow.xyz, dsx, dsy);
    float4 diffuse = i.col;
    if (lighting.on.x > 0.5) diffuse.rgb = figure_light(i.model, i.normal, i.land, face, pixel_turn(i.pos.xy));
    float4 current = diffuse;
    float4 t0 = tex0.Sample(smp0, i.uv);
    // Stage 0's patch: inside its rectangle the texels come from the patch texture (a unit's face).
    float2 w = i.uv - floor(i.uv);
    float4 tp = tex2.Sample(smp2, clamp(w * fs.patchMap.xy + fs.patchMap.zw, fs.patchInside.xy, fs.patchInside.zw));
    if (all(w >= fs.patchTo.xy) && all(w < fs.patchTo.zw)) t0 = tp;
    if (fs.colourOps.x > 1) {
        current = stage(fs.colourOps.xyz, fs.alphaOps.xyz, current, diffuse, t0, i.spec);
        if (fs.colourOps.w > 1) {
            float4 t1 = tex1.Sample(smp1, i.uv);
            current = stage(int3(fs.colourOps.w, fs.colourOps1.xy), int3(fs.alphaOps.w, fs.alphaOps1.xy), current,
                            diffuse, t1, i.spec);
        }
    }
    if (fs.switches.z != 0 && !passes(fs.switches.z, current.a, fs.switches.w)) discard;
    float3 rgb = current.rgb;
    if (fs.switches.x != 0) rgb = saturate(rgb + i.spec.rgb);
    if (shadow.on.z > 0.5) return float4((1.0 - hidden).xxx, current.a);   // the harness's shadowview
    rgb *= 1.0 - hidden * shadow.on.y;
    if (fs.switches.y != 0) rgb = lerp(fs.fogColour.rgb, rgb, i.spec.a);
    return float4(rgb, current.a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
