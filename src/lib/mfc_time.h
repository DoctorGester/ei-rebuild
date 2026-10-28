#pragma once

// MFC 4.2's CTime (timecore.cpp): a 32-bit time_t, converted through the host CRT's mktime, time, localtime and
// strftime (32-bit forms), with the current time from GetLocalTime through the game's import table as VC6's
// time() takes it (so the harness's fixed date applies).

#include "mfc_string.h"

namespace mfc::impl {

struct CTime {
    long time;

    // CTime(year, month, day, hour, minute, second, dst): mktime of those (month from 1, the year
    // in full; dst -1 lets mktime decide). Returns this.
    CTime* Construct(int year, int month, int day, int hour, int minute, int second, int dst);
    // CTime(const SYSTEMTIME&, dst): 0 before 1900.
    CTime* ConstructSystemTime(const SYSTEMTIME& at, int dst);
    // CTime(const FILETIME&, dst): the UTC file time as local time; 0 when it does not convert.
    CTime* ConstructFileTime(const FILETIME& at, int dst);
    // CTime::GetCurrentTime into `out` (returned).
    static CTime* CurrentTime(CTime* out);
    // strftime of the local time into at most 127 characters (empty when it does not fit).
    CString Format(const char* format) const;
};
static_assert(sizeof(CTime) == 4);

} // namespace mfc::impl
