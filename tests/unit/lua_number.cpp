// Script floats across the Lua boundary (src/game/script/script_lua_number.h): single precision both ways, NaN payloads
// (signalling ones too) bit for bit, through the conversions alone, through a Lua state's values and through an Eris
// persist and unpersist.
#include "../../src/game/script/script_lua_number.h"

#include "eris/lua.h"
#include "eris/lauxlib.h"
#include "eris/lualib.h"
#include "eris/eris.h"   // after lua.h, which it needs

#include <bit>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool ok, const char* what, std::uint32_t bits) {
    ++g_checks;
    if (ok) return;
    std::printf("lua_number: %s (float bits %08x)\n", what, bits);
    ++g_failures;
}

float FromBits(std::uint32_t bits) { return std::bit_cast<float>(bits); }

std::vector<std::uint32_t> Samples() {
    std::vector<std::uint32_t> bits = {
        0x00000000u, 0x80000000u, 0x00000001u, 0x807FFFFFu, 0x00800000u, 0x3F800000u, 0x3DCCCCCDu, 0x7F7FFFFFu,
        0x7F800000u, 0xFF800000u,                                // infinities
        0x7FC00000u, 0xFFC00000u, 0x7FC12345u, 0xFFFFFFFFu,      // quiet NaNs
        0x7F800001u, 0x7FA00000u, 0xFF812345u, 0x7FBFFFFFu,      // signalling NaNs
    };
    for (std::uint32_t exponent = 0; exponent < 255; ++exponent)
        for (std::uint32_t mantissa : {0x000000u, 0x000001u, 0x2AAAAAu, 0x7FFFFFu})
            bits.push_back(exponent << 23 | mantissa);
    return bits;
}

void CheckConversions() {
    for (std::uint32_t bits : Samples()) {
        const double number = scriptlua::ToLuaNumber(FromBits(bits));
        Expect(std::bit_cast<std::uint32_t>(scriptlua::FromLuaNumber(number)) == bits, "round trip", bits);
        const float value = FromBits(bits);
        if (value == value) Expect(number == static_cast<double>(value), "a number stays the float's value", bits);
        else Expect(number != number, "a NaN stays one", bits);
    }
    // A double rounds to the nearest float.
    Expect(scriptlua::FromLuaNumber(0.1) == 0.1f, "0.1 to the float 0.1f", std::bit_cast<std::uint32_t>(0.1f));
    Expect(scriptlua::FromLuaNumber(1.0 + 0x1p-30) == 1.0f, "1 + 2^-30 to 1", 0x3F800000u);
    // A NaN payload in the low 29 bits only: the quiet NaN of its sign.
    Expect(std::bit_cast<std::uint32_t>(scriptlua::FromLuaNumber(std::bit_cast<double>(0xFFF0000000000001ull))) ==
               0xFFC00000u, "low-payload NaN to the quiet NaN", 0xFFC00000u);
}

// Each value pushed, copied by Lua code into a table, read back from it; then the table persisted and unpersisted.
void CheckThroughLua() {
    lua_State* L = luaL_newstate();
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);
    const std::vector<std::uint32_t> samples = Samples();
    if (luaL_dostring(L, "return function(...) local t = {...} local u = {} for i = 1, #t do u[i] = t[i] end return u end")) {
        Expect(false, "chunk", 0);
        lua_close(L);
        return;
    }
    luaL_checkstack(L, static_cast<int>(samples.size()) + 8, "samples");
    for (std::uint32_t bits : samples) lua_pushnumber(L, scriptlua::ToLuaNumber(FromBits(bits)));
    lua_call(L, static_cast<int>(samples.size()), 1);   // the table u
    lua_newtable(L);                                  // perms
    eris_persist(L, -1, -2);
    lua_newtable(L);
    eris_unpersist(L, -1, -2);                         // the copy
    for (int table : {-5, -1}) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
            lua_rawgeti(L, table, static_cast<lua_Integer>(i + 1));
            const float back = scriptlua::FromLuaNumber(lua_tonumber(L, -1));
            lua_pop(L, 1);
            Expect(std::bit_cast<std::uint32_t>(back) == samples[i], table == -5 ? "through Lua" : "through Eris", samples[i]);
        }
    }
    lua_close(L);
}

} // namespace

int RunLuaNumberChecks() {
    g_checks = 0;
    g_failures = 0;
    CheckConversions();
    CheckThroughLua();
    std::printf("ei_unit_check: lua number: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
