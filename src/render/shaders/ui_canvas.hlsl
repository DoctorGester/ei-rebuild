// The interface canvases (render_ui.cpp, ticket N1): a window surface's pixels kept in a render target
// as the surface's memory would hold them (8 bits a channel, each channel cut to the surface format's
// bits and widened again), drawn into by GDI-like commands and blitted onto the frame. One file, several
// entry points (CMakeLists.txt compiles each one).
cbuffer vs_params : register(b0) {
    float4 rect;     // vs_rect: left, top, right, bottom (target pixels)
    float4 target;   // x, y: the target's width, height
};
cbuffer fs_params : register(b0) {
    float4 colour;   // ps_solid: the colour (0..1); ps_compose: the text colour (0..255)
    uint4 bits;      // the surface format's bits of red, green, blue; w: ps_grey's formula
    int4 source;     // ps_copy: left, top, width, height in the frame; ps_blit: left, top, texture width, height
    int4 dest;       // ps_copy: the canvas's width, height; ps_blit: the destination's left, top
    float4 scale;    // ps_blit: source texels per destination pixel (x, y)
    uint4 key;       // ps_blit: the colour key's red, green, blue (0..255); w: keyed
};

Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);

float4 ToClip(float2 p) {
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}

float4 vs_rect(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    return ToClip(lerp(rect.xy, rect.zw, corner));
}

struct GlyphVertex {
    float4 position : SV_Position;
    float2 atlas : TEXCOORD0;   // atlas texels; x < 0: an underline (full coverage)
};
GlyphVertex vs_glyph(float4 v : POSITION) {
    GlyphVertex o;
    o.position = ToClip(v.xy);
    o.atlas = v.zw;
    return o;
}

uint3 Bytes(float3 c) { return (uint3)(c * 255.0 + 0.5); }
float4 FromBytes(uint3 v) { return float4((float3)v / 255.0, 1.0); }

// The top `n` bits of a channel widened back to 8 by repeating them (render_textures.cpp's Expand).
uint Widen(uint top, uint n) {
    if (n >= 8) return top >> (n - 8);
    if (n == 0) return 0;
    uint result = 0;
    [loop] for (uint filled = 0; filled < 8; filled += n) result |= (top << (8 - n)) >> filled;
    return result;
}
uint3 Widen3(uint3 top) { return uint3(Widen(top.r, bits.r), Widen(top.g, bits.g), Widen(top.b, bits.b)); }
uint3 TopBits(uint3 v) { return v >> (8 - min(bits.rgb, 8)); }
// An 8-bit colour as the surface stores it.
uint3 Quantise(uint3 v) { return Widen3(TopBits(v)); }

float4 ps_solid(float4 pos : SV_Position) : SV_Target0 {
    return colour;
}

// A glyph run's coverage (an R8 target, blended with MAX: overlapping glyphs keep the larger).
float4 ps_glyph(GlyphVertex i) : SV_Target0 {
    if (i.atlas.x < 0.0) return float4(1.0, 0.0, 0.0, 0.0);
    return float4(tex0.Load(int3(floor(i.atlas), 0)).r, 0.0, 0.0, 0.0);
}

// The run's colour over what the canvas held (tex1, a copy) by its coverage (tex0), rounded as
// render::text::ComposeAgainstBlack does over black.
float4 ps_compose(float4 pos : SV_Position) : SV_Target0 {
    int3 p = int3(pos.xy, 0);
    uint cover = (uint)(tex0.Load(p).r * 255.0 + 0.5);
    if (cover == 0) discard;
    uint3 under = Bytes(tex1.Load(p).rgb);
    uint3 text = (uint3)colour.rgb;
    return FromBytes(Quantise((text * cover + under * (255 - cover) + 127) / 255));
}

// 005FE440's greying on the surface format's own bits (tex0: a copy of the canvas).
float4 ps_grey(float4 pos : SV_Position) : SV_Target0 {
    uint3 v = Bytes(tex0.Load(int3(pos.xy, 0)).rgb);
    if (bits.w == 0) {   // 5:6:5: (2 r + 2 g6 + b) / 16, green doubled
        uint3 t = TopBits(v);
        uint grey = (2 * t.r + 2 * t.g + t.b) >> 4;
        return FromBytes(Widen3(uint3(grey, 2 * grey, grey)));
    }
    if (bits.w == 1) {   // 5:5:5: (2 r + 4 g + b) / 16
        uint3 t = TopBits(v);
        uint grey = (2 * t.r + 4 * t.g + t.b) >> 4;
        return FromBytes(Widen3(uint3(grey, grey, grey)));
    }
    uint grey = (2 * v.r + 4 * v.g + v.b) >> 4;   // 8:8:8 and the rest: on 8-bit channels
    return FromBytes(Quantise(uint3(grey, grey, grey)));
}

// A rectangle of the frame (tex0) stretched over the canvas, nearest texel as CopyFrameTo picks it.
float4 ps_copy(float4 pos : SV_Position) : SV_Target0 {
    uint2 x = uint2(pos.xy);   // the source rectangle and sizes are clamped non-negative
    uint2 t = (uint2)source.xy + (x * (uint2)source.zw) / (uint2)dest.xy;
    return FromBytes(Quantise(Bytes(tex0.Load(int3(t, 0)).rgb)));
}

// A canvas onto the frame as blit.hlsl draws a window surface, the colour key compared on the
// stored colour.
float4 ps_blit(float4 pos : SV_Position) : SV_Target0 {
    float2 texel = (float2)source.xy + floor((pos.xy - (float2)dest.xy) * scale.xy);
    texel = clamp(texel, float2(0.0, 0.0), (float2)source.zw - 1.0);
    float4 c = tex0.Load(int3(texel, 0));
    if (key.w != 0 && all(Bytes(c.rgb) == key.rgb)) discard;
    return float4(c.rgb, 1.0);
}
