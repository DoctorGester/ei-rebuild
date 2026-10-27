#pragma once

// MFC 4.2's CTime (timecore.cpp), written out for the standalone exe (ticket,
// docs/notes/standalone.md section 2): a 32-bit time_t, converted through the CRT's mktime, time,
// localtime and strftime. In the hook build those are the game CRT's; in the standalone image the
// host CRT's (32-bit forms), with the current time from GetLocalTime through the game's import
// table as VC6's time() takes it (so the harness's fixed date applies). Registered at the library
// addresses (mfc_time.cpp), checked in game by verify_mfc_file.cpp until.

#include "mfc_string.h"

namespace mfc::impl {

struct CTime {
    long time;   // m_time

    // CTime(year, month, day, hour, minute, second, dst): mktime of those (month from 1, the year
    // in full; dst -1 lets mktime decide). Returns this.
    CTime* Construct(int year, int month, int day, int hour, int minute, int second, int dst);
    // CTime(const SYSTEMTIME&, dst): 0 before 1900.
    CTime* ConstructSystemTime(const SYSTEMTIME& at, int dst);
    // CTime(const FILETIME&, dst): the UTC file time as local time; 0 when it does not convert.
    CTime* ConstructFileTime(const FILETIME& at, int dst);
    // CTime::GetCurrentTime into `out` (returned).
    static CTime* __stdcall CurrentTime(CTime* out);
    // strftime of the local time into at most 127 characters (empty when it does not fit).
    CString Format(const char* format) const;
    // *out = the time minus
    // `span` seconds (wrapping, as SUB); returns out.
    CTime* MinusSpan(CTime* out, long span) const;
};
static_assert(sizeof(CTime) == 4);

} // namespace mfc::impl
