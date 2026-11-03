// The ground's casters into the shadow map (src/render/shaders/shadow_terrain.glsl) for SDL_GPU (the spike): only
// the faces turned away from the sun write their depth. Vertex block b0 space1, fragment block b0 space3.
struct VsParams {
    float4 light[4];   // to the shadow map
};
ConstantBuffer<VsParams> vs : register(b0, space1);
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
};
struct VsOut {
    [[vk::location(0)]] float3 normal : TEXCOORD0;
    float4 pos : SV_Position;
};
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.light[0], p), dot(vs.light[1], p), dot(vs.light[2], p), dot(vs.light[3], p));
    o.pos.z = max(o.pos.z, 0.0);
    o.normal = i.normal;
    return o;
}
#ifdef EI_FRAGMENT
struct FsParams {
    float4 sun;   // xyz: the direction the sun's light travels
};
ConstantBuffer<FsParams> fs : register(b0, space3);
void ps_main(VsOut i) {
    if (dot(i.normal, fs.sun.xyz) < 0.0) discard;
}
#endif
