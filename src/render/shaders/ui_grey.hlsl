// The greying on the surface format's own bits (tex0: a copy of the canvas; ui_canvas.hlsli).
#include "ui_canvas.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint3 v = Bytes(tex0.Load(int3(int2(pos.xy), 0)).rgb);
    if (ui.bits.w == 0) {   // 5:6:5: (2 r + 2 g6 + b) / 16, green doubled
        uint3 t = TopBits(v);
        uint grey = (2u * t.r + 2u * t.g + t.b) >> 4;
        return FromBytes(Widen3(uint3(grey, 2u * grey, grey)));
    }
    if (ui.bits.w == 1) {   // 5:5:5: (2 r + 4 g + b) / 16
        uint3 t = TopBits(v);
        uint grey = (2u * t.r + 4u * t.g + t.b) >> 4;
        return FromBytes(Widen3(uint3(grey, grey, grey)));
    }
    uint grey = (2u * v.r + 4u * v.g + v.b) >> 4;   // 8:8:8 and the rest: on 8-bit channels
    return FromBytes(Quantise(uint3(grey, grey, grey)));
}
#endif
