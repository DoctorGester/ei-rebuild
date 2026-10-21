// MFC 4.2's CTime, registered at its library addresses (mfc_time.h).
#include "mfc_time.h"

#include "ei/hook.h"
#include "gameapi.h"

#include <ctime>
#include <new>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

namespace {

// The game CRT's time functions (32-bit time_t, VC6's struct tm) as the host CRT's.
long Mktime(std::tm* at) {
    return static_cast<long>(_mktime32(at));
}

// VC6's time(): the local time from GetLocalTime, as seconds since 1970 UTC.
long Now() {
    SYSTEMTIME local;
    gameapi::GetLocalTime(&local);
    std::tm at{};
    at.tm_year = local.wYear - 1900;
    at.tm_mon = local.wMonth - 1;
    at.tm_mday = local.wDay;
    at.tm_hour = local.wHour;
    at.tm_min = local.wMinute;
    at.tm_sec = local.wSecond;
    at.tm_isdst = -1;
    return static_cast<long>(_mktime32(&at));
}

// localtime's result (one per thread), or null.
const std::tm* Localtime(const long* time) {
    static thread_local std::tm result;
    const __time32_t value = *time;
    return _localtime32_s(&result, &value) == 0 ? &result : nullptr;
}

std::size_t Strftime(char* buffer, std::size_t size, const char* format, const std::tm* at) {
    return std::strftime(buffer, size, format, at);
}

} // namespace

CTime* CTime::Construct(int year, int month, int day, int hour, int minute, int second, int dst) {
    std::tm at;   // tm_wday and tm_yday are left as the stack had them, as in the original
    at.tm_sec = second;
    at.tm_min = minute;
    at.tm_hour = hour;
    at.tm_mday = day;
    at.tm_mon = month - 1;
    at.tm_year = year - 1900;
    at.tm_isdst = dst;
    time = Mktime(&at);
    return this;
}

CTime* CTime::ConstructSystemTime(const SYSTEMTIME& at, int dst) {
    if (at.wYear < 1900) {
        time = 0;
    } else {
        CTime converted;
        converted.Construct(at.wYear, at.wMonth, at.wDay, at.wHour, at.wMinute, at.wSecond, dst);
        time = converted.time;
    }
    return this;
}

CTime* CTime::ConstructFileTime(const FILETIME& at, int dst) {
    FILETIME local;
    SYSTEMTIME system;
    if (!gameapi::FileTimeToLocalFileTime(&at, &local) || !gameapi::FileTimeToSystemTime(&local, &system)) {
        time = 0;
    } else {
        CTime converted;
        converted.ConstructSystemTime(system, dst);
        time = converted.time;
    }
    return this;
}

CTime* __stdcall CTime::CurrentTime(CTime* out) {
    out->time = Now();
    return out;
}

CString CTime::Format(const char* format) const {
    char buffer[128];
    const std::tm* at = Localtime(&time);
    if (!at || !Strftime(buffer, sizeof(buffer), format, at)) buffer[0] = '\0';
    return CString(buffer);
}

EI_REWRITTEN(0x006FE3BB, CTime::Construct);
EI_REWRITTEN(0x006FE470, CTime::ConstructSystemTime);
EI_REWRITTEN(0x006FE4BC, CTime::ConstructFileTime);
EI_REWRITTEN(0x006FE508, CTime::CurrentTime);
EI_REWRITTEN(0x006FE795, CTime::Format);
} // namespace mfc::impl
