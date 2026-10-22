#pragma once

// A CString passed by value: the caller copy-constructs it into the argument slot, the callee
// destroys it. The slot is a raw pointer.
struct StringArg {
    char* data;
};
static_assert(sizeof(StringArg) == 4);
