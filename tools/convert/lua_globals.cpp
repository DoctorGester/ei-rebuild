// The globals of a compiled Lua chunk: see lua_globals.h. Lua's internal headers stay in this file (their macros
// would reach any code after them).
#include "lua_globals.h"

#include "eris/lua.h"
#include "eris/lopcodes.h"
#include "eris/lstate.h"

#include <cstring>

namespace lua_globals {

namespace {

void FindIn(const Proto* p, std::set<std::string>* reads, std::set<std::string>* writes) {
    for (int pc = 0; pc < p->sizecode; ++pc) {
        const Instruction i = p->code[pc];
        const OpCode op = GET_OPCODE(i);
        int upvalue = 0;
        int key = 0;
        if (op == OP_GETTABUP) {
            upvalue = GETARG_B(i);
            key = GETARG_C(i);
        } else if (op == OP_SETTABUP) {
            upvalue = GETARG_A(i);
            key = GETARG_B(i);
        } else {
            continue;
        }
        const TString* name = p->upvalues[upvalue].name;
        if (!name || std::strcmp(getstr(name), "_ENV") != 0 || !ISK(key)) continue;
        const TValue* constant = &p->k[INDEXK(key)];
        if (ttisstring(constant)) (op == OP_GETTABUP ? reads : writes)->insert(svalue(constant));
    }
    for (int n = 0; n < p->sizep; ++n) FindIn(p->p[n], reads, writes);
}

}   // namespace

void Find(lua_State* L, int index, std::set<std::string>* reads, std::set<std::string>* writes) {
    if (lua_type(L, index) != LUA_TFUNCTION || lua_iscfunction(L, index)) return;
    // A Lua function's pointer is its closure.
    FindIn(static_cast<const LClosure*>(lua_topointer(L, index))->p, reads, writes);
}

}   // namespace lua_globals
