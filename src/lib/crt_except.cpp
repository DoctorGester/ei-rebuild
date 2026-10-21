// The game CRT's C++ exception pieces (S4c, docs/notes/standalone.md): _CxxThrowException and MSVC 6's
// std::exception (crt.h). Registered in both builds: they keep no state of their own (the thrown object's
// text lives on the game's heap through gamecrt::operator_new / operator_delete).
#include "crt.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "game/game_data.h"

#include <cstring>

namespace libcrt {

namespace {

// The exception record _CxxThrowException raises (the template at 00747260): the C++ exception code,
// not continuable, three parameters: the magic 0x19930520 (VC6's frame handler version), the object and
// its throw information.
constexpr unsigned long kCxxExceptionCode = 0xE06D7363;
constexpr unsigned long kCxxMagic = 0x19930520;

// std::exception's vtable in the exe (scalar deleting destructor 006E7CE6, what 006E7DBE) and what()'s
// text without one ("Unknown exception", 00747298).
const void* ExceptionVtable() { return ei::GamePointer<const void>(0x00747290); }
const char* UnknownException() { return "Unknown exception"; }

} // namespace

void __stdcall CxxThrowException(void* object, const void* throwInfo) {
    const ULONG_PTR parameters[3] = {kCxxMagic, reinterpret_cast<ULONG_PTR>(object),
                                     reinterpret_cast<ULONG_PTR>(throwInfo)};
    gameapi::RaiseException(kCxxExceptionCode, EXCEPTION_NONCONTINUABLE, 3, parameters);
}

Exception* Exception::Construct() {
    vtable = ExceptionVtable();
    text = nullptr;
    ownsText = 0;
    return this;
}

// The text is copied when the source owns its copy (a failed allocation leaves the pointer null, still
// marked owned), otherwise the pointer is shared.
Exception* Exception::CopyConstruct(const Exception& source) {
    vtable = ExceptionVtable();
    ownsText = source.ownsText;
    if (!ownsText) {
        text = source.text;
        return this;
    }
    const std::size_t size = std::strlen(source.text) + 1;
    char* copy = static_cast<char*>(gamecrt::operator_new(size));
    text = copy;
    if (copy) std::memcpy(copy, source.text, size);
    return this;
}
EI_REPLACE_METHOD(0x006E7D3F, libcrt::Exception::CopyConstruct);

Exception* Exception::Assign(const Exception& source) {
    if (this != &source) {
        Destruct();
        CopyConstruct(source);
    }
    return this;
}

void Exception::Destruct() {
    vtable = ExceptionVtable();
    if (ownsText) gamecrt::operator_delete(const_cast<char*>(text));
}

Exception* Exception::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006E7CE6, libcrt::Exception::DeletingDestruct);

const char* Exception::What() const { return text ? text : UnknownException(); }
EI_REPLACE_METHOD(0x006E7DBE, libcrt::Exception::What);

} // namespace libcrt
