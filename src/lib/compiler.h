#pragma once

#if defined(_MSC_VER)
#define EI_FORCE_INLINE __forceinline
#define EI_NOINLINE __declspec(noinline)
#else
#define EI_FORCE_INLINE inline __attribute__((always_inline))
#define EI_NOINLINE __attribute__((noinline))
#endif

#if defined(_WIN32)
#define EI_THREAD_CALL __stdcall
#else
#define EI_THREAD_CALL
#endif
