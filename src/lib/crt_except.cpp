// The game CRT's C++ exception pieces (docs/notes/standalone.md): _CxxThrowException and MSVC 6's
// std::exception (crt.h). Registered in both builds: they keep no state of their own (the thrown object's
// text lives on the game's heap through gamecrt::operator_new / operator_delete).
#include "crt.h"

#include "compiled_vtable.h"
#include "ei/hook.h"
#include "gameapi.h"
#include "game/game_data.h"

#include <cstring>

namespace libcrt {

namespace {

// The exception record _CxxThrowException raises (the template): the C++ exception code, not continuable,
// three parameters: the magic 0x19930520 (VC6's frame handler version), the object and its throw information.
constexpr unsigned long kCxxExceptionCode = 0xE06D7363;
constexpr unsigned long kCxxMagic = 0x19930520;

// std::exception's compiled table in place: 0 the scalar deleting destructor,
// 1 what; and what's text without one ("Unknown exception").
struct ExceptionTable final : ei::CompiledDeleter<ExceptionTable, &Exception::DeletingDestruct> {
    virtual const char* What() { return ObjectAs<Exception>()->What(); }   // 1
};
const void* ExceptionVtable() { return ExceptionTable::Vtable(); }
const char* UnknownException() { return "Unknown exception"; }

} // namespace

void __stdcall CxxThrowException(void* object, const void* throwInfo) {
    const ULONG_PTR parameters[3] = {kCxxMagic, reinterpret_cast<ULONG_PTR>(object),
                                     reinterpret_cast<ULONG_PTR>(throwInfo)};
    gameapi::RaiseException(kCxxExceptionCode, EXCEPTION_NONCONTINUABLE, 3, parameters);
}
EI_REWRITTEN(0x006E7819, libcrt::CxxThrowException);

Exception* Exception::Construct() {
    vtable = ExceptionVtable();
    text = nullptr;
    ownsText = 0;
    return this;
}
EI_REWRITTEN(0x006E7CD5, libcrt::Exception::Construct);

void Exception::Destruct() {
    vtable = ExceptionVtable();
    if (ownsText) gamecrt::operator_delete(const_cast<char*>(text));
}
EI_REWRITTEN(0x006E7DA8, libcrt::Exception::Destruct);

Exception* Exception::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x006E7CE6, libcrt::Exception::DeletingDestruct);

const char* Exception::What() const { return text ? text : UnknownException(); }
EI_REWRITTEN(0x006E7DBE, libcrt::Exception::What);

} // namespace libcrt
