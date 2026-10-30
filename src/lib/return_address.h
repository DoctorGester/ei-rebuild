#pragma once

// The compiler's return-address intrinsics, for the observers that note who called (the harness's records): declared
// as MSVC's <intrin.h> declares them, without the rest of that header.

extern "C" void* _ReturnAddress();            // the caller's return address
extern "C" void* _AddressOfReturnAddress();   // where the return address is on the stack
#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)
