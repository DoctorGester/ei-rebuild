@vs texture_clouds_vs
@glsl_options fixup_clipspace
@include_block rect_vertex
void main() { gl_Position = rect_position(); }
@end

@block cloud_params
layout(binding = 1) uniform cloud_params {
    ivec4 words[3];   // ps_frame: layer k's a, b, c are words 3k .. 3k + 2
    ivec4 blend;      // ps_clouds: x the weight of `to` out of 256
    ivec4 ramp[64];   // ps_clouds: the alpha (0..255) of grey level g in ramp[g / 4][g % 4]
};
@end

@block cloud_word
uint word_component(ivec4 v, uint k) {
    uvec4 word = uvec4(v);
    return k == 0u ? word.x : k == 1u ? word.y : k == 2u ? word.z : word.w;
}
@end

@fs texture_clouds_frame_fs
@include_block cloud_params
@include_block cloud_word
uint Word(uint i) { return word_component(words[i >> 2], i & 3u); }

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
    return int((edges + corners + (Noise(k, cell) >> 2)) >> 21);
}

// Layer k's cells `scale` pixels wide (scale = 1 << shift) bilinearly weighted at pixel p, out of scale x scale; the
// cell to the right of a row's last is the row's first.
int Octave(uint k, uvec2 p, int scale, uint shift) {
    uint cell = ((p.y >> shift) & 31) * 32 + ((p.x >> shift) & 31);
    uint right = ((cell + 1) & 31) != 0 ? cell + 1 : cell - 31;
    int fx = int((p.x & uint(scale - 1)));
    int fy = int((p.y & uint(scale - 1)));
    int lower = (Layer(k, right + 32) * fx + Layer(k, cell + 32) * (scale - fx)) * fy;
    int upper = (Layer(k, right) * fx + Layer(k, cell) * (scale - fx)) * (scale - fy);
    return lower + upper;
}

layout(location = 0) out uint frag_colour;
void main() {
    uvec2 p = uvec2(gl_FragCoord.xy);
    int value = Octave(0, p, 8, 3) - 0x1C00;
    value += Octave(1, p, 4, 2) * 2;
    int fine = Octave(2, p, 2, 1) + Layer(3, (p.y & 31) * 32 + (p.x & 31)) * 2;
    value = (value + fine * 4) >> 6;
    // Over 255 is 255; under 0 keeps its low byte (the game's byte store).
    frag_colour = uint(min(value, 255)) & 0xFF;
}
@end
@program texture_clouds_frame texture_clouds_vs texture_clouds_frame_fs

@fs texture_clouds_clouds_fs
@include_block cloud_params
@include_block cloud_word
layout(binding = 0) uniform utexture2D from_frame;
layout(binding = 1) uniform utexture2D to_frame;
layout(binding = 0) uniform sampler cloud_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 t = ivec2(gl_FragCoord.xy);
    uint g = (texelFetch(usampler2D(to_frame, cloud_smp), t, 0).r * uint(blend.x) +
              texelFetch(usampler2D(from_frame, cloud_smp), t, 0).r * (256 - uint(blend.x))) >>
             8;
    frag_colour = vec4(1.0, 1.0, 1.0, word_component(ramp[g >> 2], g & 3u) / 255.0);
}
@end
@program texture_clouds texture_clouds_vs texture_clouds_clouds_fs
