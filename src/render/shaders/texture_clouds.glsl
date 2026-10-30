// The global map's clouds (render_compose.cpp, render::DrawClouds). texture_clouds_frame builds one 256 x 256 frame
// of grey levels (R8UI) from the random words of its four layers (global_map_clouds.cpp keeps them), integer for
// integer as the game's CPU code did; texture_clouds blends two frames and takes each texel's alpha from the ramp.
// The words are unsigned: the block holds them as ints (sokol's uniform types) and the code reads them back as
// uints, bit for bit.
@block cloud_params
layout(binding = 1) uniform cloud_params {
    ivec4 words[3];   // the frame: layer k's a, b, c are words 3k .. 3k + 2
    ivec4 blend;      // the blend: x the weight of `to` out of 256
    ivec4 ramp[64];   // the blend: the alpha (0..255) of grey level g in ramp[g / 4][g % 4]
};
@end

@fs texture_clouds_frame_fs
@include_block cloud_params
uint Word(uint i) {
    ivec4 four = words[i >> 2];   // (sokol-shdc takes no variable component of a uniform)
    return uint(four[i & 3u]);
}

// Layer k's raw noise at cell `cell` of its 32 x 32 (wrapped to 0..1023: row cell / 32, column cell % 32).
uint Noise(uint k, uint cell) {
    cell &= 0x3FFu;
    uint seed = (cell >> 5) + (cell & 31u) * 0x39u;
    uint m = (seed << 13) ^ seed;
    return ((m * m * Word(k * 3u) + Word(k * 3u + 1u)) * m + Word(k * 3u + 2u)) & 0x1FFFFFFFu;
}

// Layer k's value (0..255) at cell `cell`: its noise smoothed with the 8 cells around it in the 1024 cells' order (a
// row's first cell's left neighbour is the row above's last).
int Layer(uint k, uint cell) {
    uint edges = (Noise(k, cell - 32u) + Noise(k, cell - 1u) + Noise(k, cell + 1u) + Noise(k, cell + 32u)) >> 3;
    uint corners = (Noise(k, cell - 33u) + Noise(k, cell - 31u) + Noise(k, cell + 31u) + Noise(k, cell + 33u)) >> 4;
    return int((edges + corners + (Noise(k, cell) >> 2)) >> 21);
}

// Layer k's cells `scale` pixels wide (scale = 1 << shift) bilinearly weighted at pixel p, out of scale x scale; the
// cell to the right of a row's last is the row's first.
int Octave(uint k, uvec2 p, int scale, uint shift) {
    uint cell = ((p.y >> shift) & 31u) * 32u + ((p.x >> shift) & 31u);
    uint right = ((cell + 1u) & 31u) != 0u ? cell + 1u : cell - 31u;
    int fx = int(p.x & uint(scale - 1));
    int fy = int(p.y & uint(scale - 1));
    int lower = (Layer(k, right + 32u) * fx + Layer(k, cell + 32u) * (scale - fx)) * fy;
    int upper = (Layer(k, right) * fx + Layer(k, cell) * (scale - fx)) * (scale - fy);
    return lower + upper;
}

layout(location = 0) out uint frag_colour;
void main() {
    uvec2 p = uvec2(gl_FragCoord.xy);
    int value = Octave(0u, p, 8, 3u) - 0x1C00;
    value += Octave(1u, p, 4, 2u) * 2;
    int fine = Octave(2u, p, 2, 1u) + Layer(3u, (p.y & 31u) * 32u + (p.x & 31u)) * 2;
    value = (value + fine * 4) >> 6;
    // Over 255 is 255; under 0 keeps its low byte (the game's byte store).
    frag_colour = uint(min(value, 255)) & 0xFFu;
}
@end
@program texture_clouds_frame rect_vs texture_clouds_frame_fs

@fs texture_clouds_fs
@include_block cloud_params
layout(binding = 0) uniform utexture2D from_frame;
layout(binding = 1) uniform utexture2D to_frame;
layout(binding = 0) uniform sampler cloud_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 t = ivec2(gl_FragCoord.xy);
    uint weight = uint(blend.x);
    uint g = (texelFetch(usampler2D(to_frame, cloud_smp), t, 0).r * weight +
              texelFetch(usampler2D(from_frame, cloud_smp), t, 0).r * (256u - weight)) >> 8;
    ivec4 four = ramp[g >> 2];
    frag_colour = vec4(1.0, 1.0, 1.0, float(four[g & 3u]) / 255.0);
}
@end
@program texture_clouds rect_vs texture_clouds_fs
