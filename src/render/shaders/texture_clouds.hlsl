// The global map's clouds (render_compose.cpp, render::DrawClouds). ps_frame builds one 256 x 256 frame of grey
// levels (R8UI) from the random words of its four layers (global_map_clouds.cpp keeps them), integer for integer as
// the game's CPU code did; ps_clouds blends two frames and takes each texel's alpha from the ramp.
cbuffer vs_params : register(b0) {
    float4 rect;     // left, top, right, bottom (target texels)
    float4 target;   // x, y: the target's width, height
};
cbuffer fs_params : register(b0) {
    uint4 words[3];   // ps_frame: layer k's a, b, c are words 3k .. 3k + 2
    uint4 blend;      // ps_clouds: x the weight of `to` out of 256
    uint4 ramp[64];   // ps_clouds: the alpha (0..255) of grey level g in ramp[g / 4][g % 4]
};

Texture2D<uint> from_frame : register(t0);
Texture2D<uint> to_frame : register(t1);

float4 vs_main(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    float2 p = lerp(rect.xy, rect.zw, corner);
    return float4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0);
}

uint Word(uint i) { return words[i >> 2][i & 3]; }

// Layer k's raw noise at cell `cell` of its 32 x 32 (wrapped to 0..1023: row cell / 32, column cell % 32).
uint Noise(uint k, uint cell) {
    cell &= 0x3FF;
    uint seed = (cell >> 5) + (cell & 31) * 0x39;
    uint m = (seed << 13) ^ seed;
    return ((m * m * Word(k * 3) + Word(k * 3 + 1)) * m + Word(k * 3 + 2)) & 0x1FFFFFFF;
}

// Layer k's value (0..255) at cell `cell`: its noise smoothed with the 8 cells around it in the 1024 cells' order (a
// row's first cell's left neighbour is the row above's last).
int Layer(uint k, uint cell) {
    uint edges = (Noise(k, cell - 32) + Noise(k, cell - 1) + Noise(k, cell + 1) + Noise(k, cell + 32)) >> 3;
    uint corners = (Noise(k, cell - 33) + Noise(k, cell - 31) + Noise(k, cell + 31) + Noise(k, cell + 33)) >> 4;
    return (int)((edges + corners + (Noise(k, cell) >> 2)) >> 21);
}

// Layer k's cells `scale` pixels wide (scale = 1 << shift) bilinearly weighted at pixel p, out of scale x scale; the
// cell to the right of a row's last is the row's first.
int Octave(uint k, uint2 p, int scale, uint shift) {
    uint cell = ((p.y >> shift) & 31) * 32 + ((p.x >> shift) & 31);
    uint right = ((cell + 1) & 31) != 0 ? cell + 1 : cell - 31;
    int fx = (int)(p.x & (scale - 1));
    int fy = (int)(p.y & (scale - 1));
    int lower = (Layer(k, right + 32) * fx + Layer(k, cell + 32) * (scale - fx)) * fy;
    int upper = (Layer(k, right) * fx + Layer(k, cell) * (scale - fx)) * (scale - fy);
    return lower + upper;
}

uint ps_frame(float4 pos : SV_Position) : SV_Target0 {
    uint2 p = uint2(pos.xy);
    int value = Octave(0, p, 8, 3) - 0x1C00;
    value += Octave(1, p, 4, 2) * 2;
    int fine = Octave(2, p, 2, 1) + Layer(3, (p.y & 31) * 32 + (p.x & 31)) * 2;
    value = (value + fine * 4) >> 6;
    // Over 255 is 255; under 0 keeps its low byte (the game's byte store).
    return (uint)min(value, 255) & 0xFF;
}

float4 ps_clouds(float4 pos : SV_Position) : SV_Target0 {
    int3 t = int3(int2(pos.xy), 0);
    uint g = (to_frame.Load(t) * blend.x + from_frame.Load(t) * (256 - blend.x)) >> 8;
    return float4(1.0, 1.0, 1.0, ramp[g >> 2][g & 3] / 255.0);
}
