// Textures the game builds from images (render_compose.cpp): a rectangle of an image drawn texel for texel
// into a rectangle of the texture's first level, over what is there (premultiplied alpha, the pipeline's
// blending) or replacing it, or the rectangle filled with one colour.
cbuffer vs_params : register(b0) {
    float4 rect;     // left, top, right, bottom (target texels)
    float4 target;   // x, y: the target's width, height
};
cbuffer fs_params : register(b0) {
    int4 offset;     // x, y: the image texel drawn at target texel (0, 0); z: 1 to fill with `colour`; w: 1 to keep
                     // only the alpha's top bit (a 1:5:5:5 texture's)
    float4 colour;
};

Texture2D<float4> tex0 : register(t0);

float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}

float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    if (offset.z != 0) return colour;
    float4 c = tex0.Load(int3(int2(pos.xy) + offset.xy, 0));
    if (offset.w != 0) c.a = c.a >= 0.5 ? 1.0 : 0.0;
    return c;
}
