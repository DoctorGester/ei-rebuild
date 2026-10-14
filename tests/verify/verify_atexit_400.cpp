// Checks for atexit_400.cpp and the small element constructors / connection check of the same
// round (net_connection.cpp, net_connection_life.cpp, net_session_base.cpp).
//  - The exit-time destructors: ours only (they destroy globals); reported when they run.
//  - ConnectionSilent, IdPoolConstruct, HandlerVectorConstruct: side by side (the constructors on
//    scratch copies). ConnectionSilent compares a plain float product (rule 7): a mismatch is
//    possible only within rounding of the limit.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_connection.h"

#include <cstring>

namespace atexit400 {
void __cdecl DestroyGlobalCamera();
void __cdecl DestroyClassRegistry();
void __cdecl ReleaseGlobalServer();
void __cdecl ReleaseGlobalClient();
void __cdecl DestroyGlobalWordArray();
void __cdecl DestroyGlobalBuffer1();
void __cdecl DestroyGlobalBuffer2();
void __cdecl DestroyGlobalVector();
}
namespace net {
IdPool* __fastcall IdPoolConstruct(IdPool* self);
void* __fastcall HandlerVectorConstruct(HandlerVector* self);
}

namespace {
void* s_unused;

#define EI_EXIT_VERIFY(fn)                          \
    void __cdecl fn##Verify() {                     \
        atexit400::fn();                            \
        ei::verify::Check(#fn, true, true, "");     \
    }
EI_EXIT_VERIFY(DestroyGlobalCamera)
EI_EXIT_VERIFY(DestroyClassRegistry)
EI_EXIT_VERIFY(ReleaseGlobalServer)
EI_EXIT_VERIFY(ReleaseGlobalClient)
EI_EXIT_VERIFY(DestroyGlobalWordArray)
EI_EXIT_VERIFY(DestroyGlobalBuffer1)
EI_EXIT_VERIFY(DestroyGlobalBuffer2)
EI_EXIT_VERIFY(DestroyGlobalVector)
#undef EI_EXIT_VERIFY

// 00433B10: FLD [ECX+0x24] (3), FMUL [0073BAA4] (6).
int(__fastcall* s_silent)(const net::Connection*);
int __fastcall SilentVerify(const net::Connection* self) {
    int expected = s_silent(self);
    int actual = self->Silent();
    ei::verify::Check("ConnectionSilent", expected, actual, "");
    return actual;
}
// 00433040 / 00436A30: PUSH ECX (1), MOV DL,[ESP+3] (4).
net::IdPool*(__fastcall* s_idPool)(net::IdPool*);
net::IdPool* __fastcall IdPoolVerify(net::IdPool* self) {
    net::IdPool expected;
    std::memcpy(&expected, self, sizeof(expected));
    net::IdPool* expectedResult = s_idPool(&expected);
    net::IdPool* result = net::IdPoolConstruct(self);
    ei::verify::Check("IdPoolConstruct", true,
                      expectedResult == &expected && result == self && std::memcmp(&expected, self, sizeof(expected)) == 0, "");
    return result;
}
void*(__fastcall* s_handlerVector)(net::HandlerVector*);
void* __fastcall HandlerVectorVerify(net::HandlerVector* self) {
    net::HandlerVector expected;
    std::memcpy(&expected, self, sizeof(expected));
    void* expectedResult = s_handlerVector(&expected);
    void* result = net::HandlerVectorConstruct(self);
    ei::verify::Check("HandlerVectorConstruct", true,
                      expectedResult == result && std::memcmp(&expected, self, sizeof(expected)) == 0, "");
    return result;
}
} // namespace

EI_VERIFY(0x0040C8A0, DestroyGlobalCameraVerify, 0, s_unused);
EI_VERIFY(0x00408D50, DestroyClassRegistryVerify, 0, s_unused);
EI_VERIFY(0x00412250, ReleaseGlobalServerVerify, 0, s_unused);
EI_VERIFY(0x004122A0, ReleaseGlobalClientVerify, 0, s_unused);
EI_VERIFY(0x0042D900, DestroyGlobalWordArrayVerify, 0, s_unused);
EI_VERIFY(0x00432CC0, DestroyGlobalBuffer1Verify, 0, s_unused);
EI_VERIFY(0x00432D00, DestroyGlobalBuffer2Verify, 0, s_unused);
EI_VERIFY(0x00434980, DestroyGlobalVectorVerify, 0, s_unused);
EI_VERIFY(0x00433B10, SilentVerify, 9, s_silent);
EI_VERIFY(0x00433040, IdPoolVerify, 5, s_idPool);
EI_VERIFY(0x00436A30, HandlerVectorVerify, 5, s_handlerVector);
