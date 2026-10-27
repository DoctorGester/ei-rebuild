// The locale and multibyte state the CRT ports read (crt_locale_data.h): the host CRT's.
#include "crt_locale_data.h"

#include <windows.h>

#include <clocale>
#include <ctype.h>
#include <cwchar>
#include <mbctype.h>

namespace libcrt {

namespace {

#ifdef EI_VECTORS_REPLAY
char replayDecimalPoint = 0;
const std::uint16_t* replayClasses = nullptr;
#endif

// The last LC_CTYPE locale name seen and its id (one per thread: the host CRT's locale state is read from any).
struct LocaleIdCache {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    unsigned id = 0;
};

} // namespace

const std::uint16_t* CharClasses() {
#ifdef EI_VECTORS_REPLAY
    if (replayClasses) return replayClasses;
#endif
    return __pctype_func();
}

unsigned CtypeLocale() {
    const wchar_t* name = ___lc_locale_name_func()[LC_CTYPE];
    if (name == nullptr) return 0;
    thread_local LocaleIdCache cache;
    if (std::wcscmp(cache.name, name) != 0) {
        wcsncpy_s(cache.name, name, _TRUNCATE);
        cache.id = LocaleNameToLCID(name, 0);
    }
    return cache.id;
}

unsigned CtypeCodePage() { return ___lc_codepage_func(); }

int MbCurMax() { return ___mb_cur_max_func(); }

char DecimalPoint() {
#ifdef EI_VECTORS_REPLAY
    if (replayDecimalPoint) return replayDecimalPoint;
#endif
    return *std::localeconv()->decimal_point;
}

const std::uint8_t* MultibyteFlags() { return __p__mbctype() + 1; }

const std::uint8_t* MultibyteCaseMap() { return __p__mbcasemap(); }

#ifdef EI_VECTORS_REPLAY
void SetReplayLocale(char decimalPoint, const std::uint16_t* classes) {
    replayDecimalPoint = decimalPoint;
    replayClasses = classes;
}
#endif

} // namespace libcrt
