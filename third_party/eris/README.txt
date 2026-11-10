Eris (https://github.com/fnuecke/eris) with the Lua 5.3.5 it ships, MIT (LICENSE: Eris's and Lua's).
Branch master-lua5.3 at commit 376bc5dd3a70d069b6206ba1bcb036cfc21360c2 (2019-10-06), its src folder without lua.c,
luac.c and the Makefile, unmodified.
Eris bundles Lua with its persistence added (version text "Lua+Eris 5.3"): eris.c and eris.h, luaopen_eris in linit.c
and lualib.h, and an eris_perm*lib function at the end of lbaselib.c, lcorolib.c, liolib.c, loadlib.c and lstrlib.c;
every other file is Lua 5.3.5's (compared with the lua/lua mirror's tag v5.3.5). The latest Lua 5.3 is 5.3.6; Eris has
no tree on it, so the Lua is the 5.3.5 Eris ships.
Used by the mission scripts' Lua runtime (src/game/script/script_lua.cpp; docs/notes/lua-scripting.md): Lua runs the
scripts, Eris persists the Lua state (running coroutines too). Built as C++, so that a Lua error unwinds through the
bindings as an exception, with LUA_USE_POSIX outside Windows and without its warnings (CMakeLists.txt).
