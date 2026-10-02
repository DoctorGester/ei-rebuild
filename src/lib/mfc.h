#pragma once

// MFC 4.2 classes statically linked into game.exe, used by rewritten code through the original
// implementations (config/library.csv). Layouts match MFC so objects can be shared with original code.

namespace mfc {

// CString: a single pointer to the character data; the reference count, length and capacity live
// in a header just before it. An empty string points at the shared afxEmptyString data.
class CString {
public:
    CString();                                    // inline in MFC: m_pchData = afxEmptyString
    CString(const char* s);                       // 007009DE
    CString(const CString& s);                    // 007006E5
    ~CString();                                   // 00700970
    const CString& operator=(const char* s);      // 00700AF9
    const CString& operator=(const CString& s);   // 00700AA9

    operator const char*() const { return data_; }

private:
    char* data_;
};
static_assert(sizeof(CString) == 4);

CString __stdcall operator+(const CString& a, const char* b);   // 00700C05
CString __stdcall operator+(const char* a, const CString& b);   // 00700C79
CString __stdcall operator+(const CString& a, const CString& b); // 00700B9F

} // namespace mfc
