// A solid rectangle (src/render/shaders/solid.glsl) for SDL_GPU (the spike): the depth clear in the middle of the
// frame and the fills. Six vertices, no buffer; vertex block b0 space1.
struct SolidParams {
    float4 rect;     // left, top, right, bottom (screen pixels)
    float4 target;   // x, y: frame width, height; z: the depth
    float4 colour;
};
ConstantBuffer<SolidParams> params : register(b0, space1);
struct VsOut {
    [[vk::location(0)]] nointerpolation float4 colour : TEXCOORD0;
    float4 pos : SV_Position;
};
VsOut vs_main(uint id : SV_VertexID) {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(params.rect.xy, params.rect.zw, corner);
    VsOut o;
    o.pos = float4(p.x * 2.0 / params.target.x - 1.0, 1.0 - p.y * 2.0 / params.target.y, params.target.z, 1.0);
    o.colour = params.colour;
    return o;
}
float4 ps_main(VsOut i) : SV_Target0 { return i.colour; }
