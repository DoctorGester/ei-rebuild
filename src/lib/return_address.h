#pragma once

// The compiler's return-address intrinsics, for the observers that note who called (the harness's records): declared
// as MSVC's <intrin.h> declares them, without the rest of that header.

#if defined(_MSC_VER)
extern "C" void* _ReturnAddress();            // the caller's return address
extern "C" void* _AddressOfReturnAddress();   // where the return address is on the stack
#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)
#define EI_RETURN_ADDRESS() _ReturnAddress()
#else
#define EI_RETURN_ADDRESS() __builtin_extract_return_addr(__builtin_return_address(0))
#endif
