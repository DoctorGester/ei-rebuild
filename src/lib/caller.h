#pragma once

// Caller capture, for the observers that note who called (the harness's records: random.cpp, crt.cpp,
// core_streams.cpp). Used in a function's body: EI_RETURN_ADDRESS() is that function's return address,
// EI_CALLER_FRAME() the stack from the word that holds it (the observers scan on from there). Macros, not functions: the
// debug build inlines nothing, and a helper would return into itself.

#if defined(_MSC_VER)
extern "C" void* _ReturnAddress();            // declared as <intrin.h> declares them, without the rest of it
extern "C" void* _AddressOfReturnAddress();
#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)
#define EI_RETURN_ADDRESS() _ReturnAddress()
#define EI_CALLER_FRAME() static_cast<void* const*>(_AddressOfReturnAddress())
#else
#define EI_RETURN_ADDRESS() __builtin_extract_return_addr(__builtin_return_address(0))
#define EI_CALLER_FRAME() (static_cast<void* const*>(__builtin_frame_address(0)) + 1)
#endif
