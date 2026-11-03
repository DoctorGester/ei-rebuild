// A canvas filled with one colour (ui_canvas.hlsli).
#include "ui_canvas.hlsli"
#ifdef EI_FRAGMENT
float4 ps_main(float4 pos : SV_Position) : SV_Target0 { return ui.colour; }
#endif
