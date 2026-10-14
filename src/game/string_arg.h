#pragma once

// A CString passed by value: the caller copy-constructs it into the argument slot, the callee
// destroys it. Naked forwarders cannot take a class by value, so the slot is a raw pointer.
struct StringArg {
    char* data;
};
static_assert(sizeof(StringArg) == 4);
