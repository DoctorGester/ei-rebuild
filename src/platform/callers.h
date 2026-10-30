#pragma once

// Trace observers capture their caller before adding a platform frame. These helpers must be inlined even
// in debug builds; ordinary game functions do not need compiler-specific inlining or stack intrinsics.
#if defined(_MSC_VER)
extern "C" void* _ReturnAddress();
extern "C" void* _AddressOfReturnAddress();
#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)
#define EI_PLATFORM_INLINE __forceinline
#else
#define EI_PLATFORM_INLINE inline __attribute__((always_inline))
#endif

namespace platform {

EI_PLATFORM_INLINE void* ReturnAddress() {
#if defined(_MSC_VER)
    return _ReturnAddress();
#else
    return __builtin_extract_return_addr(__builtin_return_address(0));
#endif
}

// The Windows harness scans 64 stack words beginning at the caller's return address. Other ABIs expose
// the direct caller followed by zeros. The supplied words remain valid only during the observer call.
template <class Observer, class... Arguments>
EI_PLATFORM_INLINE void ObserveCallers(Observer observer, Arguments... arguments) {
#if defined(_MSC_VER)
    observer(arguments..., static_cast<void* const*>(_AddressOfReturnAddress()));
#else
    void* callers[64] = {ReturnAddress()};
    observer(arguments..., callers);
#endif
}

}   // namespace platform

#undef EI_PLATFORM_INLINE
