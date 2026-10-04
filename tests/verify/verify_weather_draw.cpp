// Checks for the weather's draw (weather_draw.cpp).
#include "verify.h"

int __fastcall WeatherDraw(void* self, void*, void* renderer);

// 004E5680 steps and draws the weather: ours only. Checked: the result is 0 or 1, and 0 only
// without a world or with a mode other than none, rain and snow.
static void* s_weatherDraw;
static int __fastcall WeatherDrawVerify(void* self, void*, void* renderer) {
    auto* p = static_cast<unsigned char*>(self);
    bool world = *reinterpret_cast<void**>(p + 0x20) != nullptr;
    int result = WeatherDraw(self, nullptr, renderer);
    int mode = *reinterpret_cast<int*>(p + 0xB4);
    bool ok = (result == 0 || result == 1) && (result == 1 || !world || mode < 0 || mode > 2);
    ei::verify::Check("WeatherDraw", true, ok, "result %d mode %d", result, mode);
    return result;
}
EI_VERIFY(0x004E5680, WeatherDrawVerify, 0, s_weatherDraw);
