#pragma once

// The NaN audit (C16c; docs/notes/cleanup-tooling.md, "SSE2 build"). The game's comparisons are plain
// C++: a NaN operand compares as IEEE says, where the original's FCOM / FNSTSW / TEST AH treated an
// unordered result as "equal" or "less". No NaN reaches those comparisons in the scenario suite; one
// that does is a bug upstream. EI_NAN_AUDIT(values...) next to such a comparison logs to ei_hook.log,
// once per site, when one of the values is NaN, in the verify and debug builds; in release it is
// nothing. Code whose purpose is detecting NaN (data checks) tests it itself (x87::IsNaN).

#include "ei/x87.h"

#if defined(EI_NAN_AUDIT_LOG)
#include "hook/log.h"

namespace ei::nanaudit {
template <class... T>
bool AnyNaN(T... values) {
    return (x87::IsNaN(values) || ...);
}
} // namespace ei::nanaudit

#define EI_NAN_AUDIT(...)                                                                       \
    do {                                                                                        \
        static bool eiNanAuditReported = false;                                                 \
        if (!eiNanAuditReported && ::ei::nanaudit::AnyNaN(__VA_ARGS__)) {                       \
            eiNanAuditReported = true;                                                          \
            ::ei::Log("NaN audit: a NaN compared at %s:%d", __FILE__, __LINE__);                \
        }                                                                                       \
    } while (0)
#else
#define EI_NAN_AUDIT(...) ((void)0)
#endif
