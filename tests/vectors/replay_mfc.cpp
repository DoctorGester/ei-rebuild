// ei_vectors' MFC core (src/lib/mfc.h, EI_VECTORS_REPLAY): CString and CPlex written as MFC 4.2 writes
// them (strcore.cpp, strex.cpp, plex.cpp), on the replay's heap (gamecrt::operator_new, replay_crt.cpp).
// A string's data is MFC's CStringData: references, length, allocated length, then the characters; the
// empty string is a shared block whose reference count is -1. Allocations are exactly the length (MFC
// rounds small ones up to its fixed-size blocks; only when a later write reallocates differs).
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"
#include "../../src/lib/mfc_string.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct StringData {
    long refs;
    int length;
    int alloc;
    char* Chars() { return reinterpret_cast<char*>(this + 1); }
};

struct {
    StringData data;
    char nul;
} g_nil = {{-1, 0, 0}, 0};

char* NilChars() { return g_nil.data.Chars(); }

StringData* DataOf(const char* chars) { return reinterpret_cast<StringData*>(const_cast<char*>(chars)) - 1; }

int SafeLength(const char* s) { return s ? static_cast<int>(std::strlen(s)) : 0; }

// AllocBuffer: a block for `length` characters (terminated), one reference.
char* Allocate(int length) {
    if (length == 0) return NilChars();
    auto* data = static_cast<StringData*>(gamecrt::operator_new(sizeof(StringData) + length + 1));
    data->refs = 1;
    data->length = length;
    data->alloc = length;
    data->Chars()[length] = 0;
    return data->Chars();
}

void ReleaseData(char* chars) {
    StringData* data = DataOf(chars);
    if (data != &g_nil.data && --data->refs <= 0) gamecrt::operator_delete(data);
}

} // namespace

namespace mfc {

// The private helpers of MFC's CString work on data_ through these.
struct StringCore {
    static char*& Chars(CString& s) { return *reinterpret_cast<char**>(&s); }
    static const char* Chars(const CString& s) { return static_cast<const char*>(s); }

    static void Release(CString& s) {
        ReleaseData(Chars(s));
        Chars(s) = NilChars();
    }
    // AllocBeforeWrite + the copy (AssignCopy).
    static void AssignCopy(CString& s, int length, const char* from) {
        StringData* data = DataOf(Chars(s));
        if (data->refs > 1 || length > data->alloc) {
            Release(s);
            Chars(s) = Allocate(length);
        }
        if (DataOf(Chars(s)) == &g_nil.data) return;   // length 0: the shared empty string
        std::memcpy(Chars(s), from, length);
        DataOf(Chars(s))->length = length;
        Chars(s)[length] = 0;
    }
};

CString::CString() : data_(NilChars()) {}

CString::CString(const char* s) : data_(NilChars()) {
    if (s && (reinterpret_cast<std::uintptr_t>(s) >> 16) == 0) {
        std::fprintf(stderr, "ei_vectors: CString from string resource %u\n", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(s)));
        std::abort();
    }
    int length = SafeLength(s);
    if (length != 0) {
        data_ = Allocate(length);
        std::memcpy(data_, s, length);
    }
}

CString::CString(const char* s, int length) : data_(NilChars()) {
    if (length != 0) {
        data_ = Allocate(length);
        std::memcpy(data_, s, length);
    }
}

CString::CString(const CString& s) : data_(NilChars()) {
    if (DataOf(s.data_)->refs >= 0) {
        data_ = s.data_;
        ++DataOf(data_)->refs;
    } else {
        *this = s.data_;
    }
}

CString::~CString() { ReleaseData(data_); }

const CString& CString::operator=(const char* s) {
    StringCore::AssignCopy(*this, SafeLength(s), s);
    return *this;
}

const CString& CString::operator=(const CString& s) {
    if (data_ != s.data_) {
        StringData* mine = DataOf(data_);
        if ((mine->refs < 0 && mine != &g_nil.data) || DataOf(s.data_)->refs < 0) {
            StringCore::AssignCopy(*this, DataOf(s.data_)->length, s.data_);
        } else {
            StringCore::Release(*this);
            data_ = s.data_;
            ++DataOf(data_)->refs;
        }
    }
    return *this;
}

} // namespace mfc

// The empty string of the library's CString (mfc_string.h) is this one.
mfc::impl::CStringData* mfc::impl::DataNil() { return reinterpret_cast<CStringData*>(&g_nil.data); }
char* mfc::impl::PchNil() { return NilChars(); }
