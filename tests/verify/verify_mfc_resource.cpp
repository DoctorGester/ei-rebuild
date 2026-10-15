// Checks for MFC's resource strings, the exceptions' error messages and CFile's name slots
// (src/lib/mfc_resource.cpp, mfc_exception.cpp, mfc_file.cpp, mfc_window.cpp; ticket S8b).
//
// Against the original, ours first (SV2f / SV2h): CString::LoadString, the AfxFormatStrings family
// and CFile's name getters fill the real string, then the original fills a scratch one made afterwards
// (freed after the comparison); GetErrorMessage of the file and archive exceptions write the caller's
// buffer, the original a host-heap one; CSimpleException's InitString / GetErrorMessage change the
// object, so the original runs on a copy taken before ours ran (no allocations: the message is a
// fixed array) and the two objects are compared; GetStatus, AfxGetFileName and AfxGetFileTitle fill
// host memory for the original. Ours only: AfxLoadString (its first instruction after PUSH ESI is a
// CALL; the wrapper checks the buffer against the length and that the original's module state has
// the null resource handle the image's ImageModuleState has), AfxMessageBox(UINT) and ReportError
// (modal boxes). The suite reaches none of these (error paths), so a start-up check drives them
// through their addresses: every MFC prompt id the library loads, the file and archive causes, the
// four static exceptions on copies, format strings and CFile names on a temporary file.
#include "verify.h"

#include "../../src/hook/log.h"
#include "../../src/lib/gameapi.h"
#include "../../src/lib/mfc_exception.h"
#include "../../src/lib/mfc_file.h"
#include "../../src/lib/mfc_resource.h"
#include "../../src/lib/mfc_state.h"
#include "../../src/lib/mfc_window.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {

using ei::verify::Check;
using mfc::impl::CArchiveException;
using mfc::impl::CException;
using mfc::impl::CExceptionVirtuals;
using mfc::impl::CFile;
using mfc::impl::CFileException;
using mfc::impl::CFileStatus;
using mfc::impl::CSimpleException;
using mfc::impl::CString;
void* s_unused;

const char* Text(const CString& s) { return static_cast<const char*>(s); }
bool Same(const CString& a, const CString& b) { return std::strcmp(Text(a), Text(b)) == 0; }

// Storage for an object built in place.
template <class T>
struct Slot {
    alignas(T) unsigned char raw[sizeof(T)];
    T* Get() { return reinterpret_cast<T*>(raw); }
};

// The exception classes' vtables (ReportError is slot 4 of each).
bool IsExceptionVtable(const void* vtable) {
    for (std::uintptr_t known : {0x00745E4Cu, 0x0074646Cu, 0x00746484u, 0x007465F8u, 0x00746610u, 0x00746808u})
        if (vtable == reinterpret_cast<const void*>(known)) return true;
    return false;
}

// ---- Resource strings ----------------------------------------------------------------------------

// 00702B0E: PUSH EBP; MOV EBP,ESP; SUB ESP,0x104 (9 bytes).
BOOL(__fastcall* s_loadString)(CString*, void*, unsigned);
BOOL __fastcall LoadStringVerify(CString* self, void*, unsigned id) {
    const BOOL result = self->LoadResourceString(id);
    CString expected;
    const BOOL expectedResult = s_loadString(&expected, nullptr, id);
    Check("CString::LoadString", true, result == expectedResult && Same(expected, *self), "id %#x: %d \"%.80s\" (original %d \"%.80s\")", id,
          result, Text(*self), expectedResult, Text(expected));
    return result;
}

int __stdcall LoadVerify(unsigned id, char* buffer, unsigned size) {
    const int length = mfc::impl::AfxLoadString(id, buffer, size);
    const HINSTANCE resources = mfc::impl::AfxGetModuleState()->resources;
    const HINSTANCE image = mfc::impl::ImageModuleState()->resources;
    const bool consistent = length >= 0 && (length == 0 ? buffer[0] == '\0' : gameapi::lstrlenA(buffer) == length);
    Check("AfxLoadString", true, consistent && resources == image, "id %#x, size %u: %d, resource handle %p (image %p)", id, size, length,
          resources, image);
    return length;
}

// The format functions: the text `out` had kept in our memory, so a format id that does not load
// leaves the original's scratch string with the same text.
// 0070B1AD: PUSH EBP; MOV EBP,ESP; PUSH EBX; PUSH ESI (5 bytes).
void(__stdcall* s_formatStrings)(CString&, const char*, const char* const*, int);
void __stdcall FormatStringsVerify(CString& out, const char* format, const char* const* strings, int count) {
    const std::string before = Text(out);
    mfc::impl::AfxFormatStrings(out, format, strings, count);
    CString expected(before.c_str());
    s_formatStrings(expected, format, strings, count);
    Check("AfxFormatStrings", true, Same(expected, out), "\"%.60s\" (%d): \"%.80s\" (original \"%.80s\")", format, count, Text(out),
          Text(expected));
}

// 0070B173: PUSH EBP; MOV EBP,ESP; SUB ESP,0x100 (9 bytes).
void(__stdcall* s_formatStringsId)(CString&, unsigned, const char* const*, int);
void __stdcall FormatStringsIdVerify(CString& out, unsigned formatId, const char* const* strings, int count) {
    const std::string before = Text(out);
    mfc::impl::AfxFormatStringsId(out, formatId, strings, count);
    CString expected(before.c_str());
    s_formatStringsId(expected, formatId, strings, count);
    Check("AfxFormatStrings(UINT)", true, Same(expected, out), "id %#x (%d): \"%.80s\" (original \"%.80s\")", formatId, count, Text(out),
          Text(expected));
}

// 0070B2AC: LEA EAX,[ESP+0xC]; PUSH 1 (6 bytes).
void(__stdcall* s_formatString1)(CString&, unsigned, const char*);
void __stdcall FormatString1Verify(CString& out, unsigned formatId, const char* string) {
    const std::string before = Text(out);
    mfc::impl::AfxFormatString1(out, formatId, string);
    CString expected(before.c_str());
    s_formatString1(expected, formatId, string);
    Check("AfxFormatString1", true, Same(expected, out), "id %#x \"%.40s\": \"%.80s\" (original \"%.80s\")", formatId, string ? string : "(null)",
          Text(out), Text(expected));
}

int __stdcall MessageBoxIdVerify(unsigned promptId, unsigned type, unsigned helpId) {
    const void* app = mfc::impl::AfxGetModuleState()->currentApp;
    Check("AfxMessageBox(UINT)", true, app == nullptr, "prompt %#x, application %p", promptId, app);
    return mfc::impl::MessageBoxId(promptId, type, helpId);
}

// ---- Exceptions ----------------------------------------------------------------------------------

int __fastcall ReportErrorVerify(CException* self, void*, unsigned type, unsigned error) {
    Check("CException::ReportError", true, IsExceptionVtable(self->vtable), "vtable %p, error %#x", self->vtable, error);
    return self->ReportError(type, error);
}

// The two cause messages: the caller's buffer from ours, a host buffer from the original.
// 00703135 / 0070A795: MOV EAX,imm32 (5 bytes).
template <int Id, class Exception>
struct CauseMessage {
    static inline BOOL(__fastcall* original)(Exception*, void*, char*, unsigned, unsigned*);
    static inline const char* name;
    static BOOL __fastcall Verify(Exception* self, void*, char* error, unsigned maxError, unsigned* helpContext) {
        const BOOL result = self->GetErrorMessage(error, maxError, helpContext);
        std::vector<char> expected(maxError + 1, '\0');
        unsigned expectedHelp = 0;
        const BOOL expectedResult = original(self, nullptr, expected.data(), maxError, helpContext ? &expectedHelp : nullptr);
        const bool sameText = maxError == 0 || std::strcmp(error, expected.data()) == 0;
        const bool sameHelp = !helpContext || *helpContext == expectedHelp;
        Check(name, true, result == expectedResult && sameText && sameHelp, "cause %d, max %u: %d \"%.80s\" help %#x (original %d \"%.80s\" %#x)",
              self->cause, maxError, result, maxError ? error : "", helpContext ? *helpContext : 0u, expectedResult,
              expected.data(), expectedHelp);
        return result;
    }
};
using FileMessage = CauseMessage<0, CFileException>;
using ArchiveMessage = CauseMessage<1, CArchiveException>;

// CSimpleException: the original on a copy of the object as it was before ours ran.
// 006FE346: MOV EAX,[ESP+0xC]; PUSH ESI (5 bytes).
BOOL(__fastcall* s_simpleMessage)(CSimpleException*, void*, char*, unsigned, unsigned*);
BOOL __fastcall SimpleMessageVerify(CSimpleException* self, void*, char* error, unsigned maxError, unsigned* helpContext) {
    CSimpleException twin;
    std::memcpy(&twin, self, sizeof(twin));
    const BOOL result = self->GetErrorMessage(error, maxError, helpContext);
    std::vector<char> expected(maxError + 1, '\0');
    unsigned expectedHelp = 0;
    const BOOL expectedResult = s_simpleMessage(&twin, nullptr, expected.data(), maxError, helpContext ? &expectedHelp : nullptr);
    const bool sameText = maxError == 0 || std::strcmp(error, expected.data()) == 0;
    const bool sameHelp = !helpContext || *helpContext == expectedHelp;
    const bool sameObject = std::memcmp(&twin, self, sizeof(twin)) == 0;
    Check("CSimpleException::GetErrorMessage", true, result == expectedResult && sameText && sameHelp && sameObject,
          "id %#x: %d \"%.80s\" (original %d \"%.80s\"), object %s", self->resourceId, result, maxError ? error : "", expectedResult,
          expected.data(), sameObject ? "same" : "differs");
    return result;
}

// 006FE31D: PUSH ESI; MOV ESI,ECX; PUSH 0x80 (8 bytes).
void(__fastcall* s_initString)(CSimpleException*);
void __fastcall InitStringVerify(CSimpleException* self) {
    CSimpleException twin;
    std::memcpy(&twin, self, sizeof(twin));
    self->InitString();
    s_initString(&twin);
    Check("CSimpleException::InitString", true, std::memcmp(&twin, self, sizeof(twin)) == 0, "id %#x: loaded %d \"%.80s\" (original %d \"%.80s\")",
          self->resourceId, self->loaded, self->message, twin.loaded, twin.message);
}

// ---- CFile's names -------------------------------------------------------------------------------

// GetFileName / GetFileTitle: MOV EAX,imm32 (5 bytes); GetFilePath: PUSH EBP; MOV EBP,ESP;
// SUB ESP,0x11C (9).
using NameGetter = CString*(__fastcall*)(const CFile*, void*, CString*);
template <int Id>
struct FileString {
    static inline NameGetter original;
    static inline CString (CFile::*ours)() const;
    static inline const char* name;
    static CString* __fastcall Verify(const CFile* self, void*, CString* result) {
        ::new (result) CString((self->*ours)());
        Slot<CString> expected;
        original(self, nullptr, expected.Get());
        Check(name, true, Same(*expected.Get(), *result), "\"%.80s\" (original \"%.80s\")", Text(*result), Text(*expected.Get()));
        expected.Get()->~CString();
        return result;
    }
};
using FileName = FileString<0>;
using FileTitle = FileString<1>;
using FilePath = FileString<2>;

// 00702D64: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes).
BOOL(__fastcall* s_getStatus)(const CFile*, void*, CFileStatus*);
BOOL __fastcall GetStatusVerify(const CFile* self, void*, CFileStatus* status) {
    const BOOL result = self->GetStatus(*status);
    CFileStatus expected;
    const BOOL expectedResult = s_getStatus(self, nullptr, &expected);
    Check("CFile::GetStatus", true, result == expectedResult && std::memcmp(&expected, status, sizeof(expected)) == 0,
          "\"%.80s\": %d size %ld attributes %#x times %ld %ld %ld (original %d %ld %#x %ld %ld %ld)", Text(self->fileName), result, status->size,
          status->attribute, status->created.time, status->modified.time, status->accessed.time, expectedResult, expected.size,
          expected.attribute, expected.created.time, expected.modified.time, expected.accessed.time);
    return result;
}

// AfxGetFileName: PUSH ESI; MOV ESI,[ESP+8] (5 bytes). AfxGetFileTitle: PUSH EBP; MOV EBP,ESP;
// SUB ESP,0x104 (9).
using PathPart = unsigned(__stdcall*)(const char*, char*, unsigned);
template <int Id>
struct FilePart {
    static inline PathPart original;
    static inline PathPart ours;
    static inline const char* name;
    static unsigned __stdcall Verify(const char* path, char* out, unsigned max) {
        const unsigned result = ours(path, out, max);
        std::vector<char> expected(max + 1, '\0');
        const unsigned expectedResult = original(path, out ? expected.data() : nullptr, max);
        const bool sameText = !out || max == 0 || std::strcmp(out, expected.data()) == 0;
        Check(name, true, result == expectedResult && sameText, "\"%.80s\" max %u: %u \"%.80s\" (original %u \"%.80s\")", path, max, result,
              out && max ? out : "", expectedResult, expected.data());
        return result;
    }
};
using FileNamePart = FilePart<0>;
using FileTitlePart = FilePart<1>;

// ---- Start-up check ------------------------------------------------------------------------------

// The functions through their addresses (the wrappers above once the hooks are installed).
__declspec(naked) BOOL __fastcall LoadStringAt(CString*, void*, unsigned) EI_ORIGINAL(0x00702B0E)
__declspec(naked) int __stdcall AfxLoadStringAt(unsigned, char*, unsigned) EI_ORIGINAL(0x00702B92)
__declspec(naked) void __fastcall InitStringAt(CSimpleException*) EI_ORIGINAL(0x006FE31D)
__declspec(naked) void __stdcall FormatStringsAt(CString&, const char*, const char* const*, int) EI_ORIGINAL(0x0070B1AD)
__declspec(naked) void __stdcall FormatStringsIdAt(CString&, unsigned, const char* const*, int) EI_ORIGINAL(0x0070B173)
__declspec(naked) void __stdcall FormatString1At(CString&, unsigned, const char*) EI_ORIGINAL(0x0070B2AC)
__declspec(naked) unsigned __stdcall FileNamePartAt(const char*, char*, unsigned) EI_ORIGINAL(0x0070D0D2)
__declspec(naked) unsigned __stdcall FileTitlePartAt(const char*, char*, unsigned) EI_ORIGINAL(0x00701AC9)
__declspec(naked) CString* __fastcall FileNameAt(const CFile*, void*, CString*) EI_ORIGINAL(0x00702C31)
__declspec(naked) CString* __fastcall FileTitleAt(const CFile*, void*, CString*) EI_ORIGINAL(0x00702CB3)
__declspec(naked) CString* __fastcall FilePathAt(const CFile*, void*, CString*) EI_ORIGINAL(0x00702D35)

// MFC's four static simple exceptions (resource, user, memory, not supported).
const CSimpleException* StaticException(int index) {
    static const std::uintptr_t addresses[4] = {0x007C8C58, 0x007C8BC0, 0x007C8DE8, 0x007C8D50};
    return reinterpret_cast<const CSimpleException*>(addresses[index]);
}

// Every prompt the library code loads, a game string and two missing ids; how many loaded is logged.
void StringsCheck() {
    std::vector<unsigned> ids = {mfc::impl::kIdsUnnamedFile, mfc::impl::kIdpNoErrorAvailable, 0xF021, 0xF022, 0xF023, 0xF024, 0x65, 0x7FFF, 0xFFFF};
    for (unsigned cause = 0; cause <= CFileException::endOfFile; ++cause) ids.push_back(mfc::impl::kIdpFileNone + cause);
    for (unsigned cause = 0; cause <= CArchiveException::badSchema; ++cause) ids.push_back(mfc::impl::kIdpArchiveNone + cause);
    int loaded = 0;
    for (unsigned id : ids) {
        CString text;
        if (LoadStringAt(&text, nullptr, id)) ++loaded;
        char truncated[8];
        AfxLoadStringAt(id, truncated, sizeof(truncated));
    }
    ei::Log("mfc resource strings (start-up): %d of %u prompt ids load", loaded, static_cast<unsigned>(ids.size()));
}

void FormatCheck() {
    const char* const strings[2] = {"one", nullptr};
    const char* const formats[] = {"%1 and %2", "%A%Z%9%3", "100%", "%%1", "a%", "plain", "", "%1%1%1", "[%2]"};
    for (const char* format : formats) {
        CString out("previous");
        FormatStringsAt(out, format, strings, 2);
    }
    for (unsigned id : {mfc::impl::kIdpFileNone + 2, mfc::impl::kIdpArchiveNone + 3, mfc::impl::kIdsUnnamedFile, 0x7FFFu}) {
        CString out("kept when the format does not load");
        FormatStringsIdAt(out, id, strings, 1);
        CString one;
        FormatString1At(one, id, "x:\\folder\\name.ext");
    }
}

// Each cause of the two classes with and without a file name, with a short buffer and no help context.
void CauseMessagesCheck() {
    char message[512];
    unsigned help = 0;
    for (int cause = CFileException::none; cause <= CFileException::endOfFile; ++cause) {
        for (const char* name : {"", "x:\\folder\\name.ext"}) {
            Slot<CFileException> e;
            ::new (&e.Get()->fileName) CString();
            e.Get()->InitEmpty();
            e.Get()->cause = cause;
            e.Get()->fileName = name;
            auto* slots = reinterpret_cast<CExceptionVirtuals*>(e.Get());
            slots->GetErrorMessage(message, sizeof(message), &help);
            slots->GetErrorMessage(message, 16, nullptr);
            e.Get()->Destruct();
        }
    }
    for (int cause = CArchiveException::none; cause <= CArchiveException::badSchema; ++cause) {
        for (const char* name : {"", "archive.sav"}) {
            Slot<CArchiveException> e;
            reinterpret_cast<CException*>(e.Get())->Construct();
            e.Get()->vtable = CArchiveException::Vtable();
            e.Get()->cause = cause;
            ::new (&e.Get()->fileName) CString(name);
            auto* slots = reinterpret_cast<CExceptionVirtuals*>(e.Get());
            slots->GetErrorMessage(message, sizeof(message), &help);
            slots->GetErrorMessage(message, 16, nullptr);
            e.Get()->Destruct();
        }
    }
}

// The static exceptions on copies: as they are, not yet loaded (through InitString), and loaded.
void SimpleMessagesCheck() {
    char message[512];
    unsigned help = 0;
    for (int index = 0; index < 4; ++index) {
        CSimpleException copy;
        std::memcpy(&copy, StaticException(index), sizeof(copy));
        copy.initialized = FALSE;
        copy.loaded = FALSE;
        auto* slots = reinterpret_cast<CExceptionVirtuals*>(&copy);
        slots->GetErrorMessage(message, sizeof(message), &help);   // loads
        slots->GetErrorMessage(message, 8, nullptr);               // loaded
        copy.initialized = FALSE;
        InitStringAt(&copy);
        copy.resourceId = 0x7FFF;   // missing
        copy.initialized = FALSE;
        slots->GetErrorMessage(message, sizeof(message), &help);
    }
}

// Path parts of a few shapes, with a null output for the size and a short one.
void PathPartsCheck() {
    const char* const paths[] = {"c:\\folder\\name.ext", "name", "dir/sub:x.y", "", "c:\\folder\\", "\\\\server\\share\\a.b.c", "no.ext."};
    for (const char* path : paths) {
        char out[MAX_PATH];
        FileNamePartAt(path, out, sizeof(out));
        FileNamePartAt(path, nullptr, 0);
        FileNamePartAt(path, out, 3);
        FileTitlePartAt(path, out, sizeof(out));
        FileTitlePartAt(path, nullptr, 0);
        FileTitlePartAt(path, out, 3);
    }
}

void Names(const CFile* file) {
    Slot<CString> name, title, path;
    FileNameAt(file, nullptr, name.Get());
    FileTitleAt(file, nullptr, title.Get());
    FilePathAt(file, nullptr, path.Get());
    name.Get()->~CString();
    title.Get()->~CString();
    path.Get()->~CString();
}

// An open temporary file (times, size, attributes), a closed one with a name, and an open handle with
// no name.
void FileNamesCheck() {
    char temp[MAX_PATH];
    if (!::GetTempPathA(MAX_PATH, temp)) return;
    char leaf[64];
    std::snprintf(leaf, sizeof(leaf), "ei_mfc_resource_%lu.dat", ::GetCurrentProcessId());
    const std::string path = std::string(temp) + leaf;
    const HANDLE handle = ::CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(handle, "twelve bytes", 12, &written, nullptr);
    Slot<CFile> file;
    file.Get()->Construct();
    file.Get()->handle = handle;
    file.Get()->fileName = path.c_str();
    Names(file.Get());
    file.Get()->fileName = static_cast<const char*>(nullptr);
    Names(file.Get());
    file.Get()->handle = CFile::kNoFile;
    file.Get()->fileName = "c:/folder/closed.name.txt";
    Names(file.Get());
    file.Get()->Destruct();
    ::CloseHandle(handle);
    ::DeleteFileA(path.c_str());
}

void ResourceStartupCheck() {
    const mfc::impl::ModuleState* state = mfc::impl::AfxGetModuleState();
    Check("module state handles (start-up)", true, state->instance == nullptr && state->resources == nullptr,
          "instance %p, resources %p (the image's are null)", state->instance, state->resources);
    StringsCheck();
    FormatCheck();
    CauseMessagesCheck();
    SimpleMessagesCheck();
    PathPartsCheck();
    FileNamesCheck();
}
EI_STARTUP_CHECK(ResourceStartupCheck);

struct Init {
    Init() {
        FileMessage::name = "CFileException::GetErrorMessage";
        ArchiveMessage::name = "CArchiveException::GetErrorMessage";
        FileName::name = "CFile::GetFileName";
        FileName::ours = &CFile::GetFileName;
        FileTitle::name = "CFile::GetFileTitle";
        FileTitle::ours = &CFile::GetFileTitle;
        FilePath::name = "CFile::GetFilePath";
        FilePath::ours = &CFile::GetFilePath;
        FileNamePart::name = "AfxGetFileName";
        FileNamePart::ours = &mfc::impl::AfxGetFileName;
        FileTitlePart::name = "AfxGetFileTitle";
        FileTitlePart::ours = &mfc::impl::AfxGetFileTitle;
    }
} s_init;

} // namespace

EI_VERIFY(0x00702B0E, LoadStringVerify, 9, s_loadString);
EI_VERIFY(0x00702B92, LoadVerify, 0, s_unused);
EI_VERIFY(0x0070B1AD, FormatStringsVerify, 5, s_formatStrings);
EI_VERIFY(0x0070B173, FormatStringsIdVerify, 9, s_formatStringsId);
EI_VERIFY(0x0070B2AC, FormatString1Verify, 6, s_formatString1);
EI_VERIFY(0x0070ACCC, MessageBoxIdVerify, 0, s_unused);
EI_VERIFY(0x0070209C, ReportErrorVerify, 0, s_unused);
EI_VERIFY(0x00703135, FileMessage::Verify, 5, FileMessage::original);
EI_VERIFY(0x0070A795, ArchiveMessage::Verify, 5, ArchiveMessage::original);
EI_VERIFY(0x006FE346, SimpleMessageVerify, 5, s_simpleMessage);
EI_VERIFY(0x006FE31D, InitStringVerify, 8, s_initString);
EI_VERIFY(0x00702C31, FileName::Verify, 5, FileName::original);
EI_VERIFY(0x00702CB3, FileTitle::Verify, 5, FileTitle::original);
EI_VERIFY(0x00702D35, FilePath::Verify, 9, FilePath::original);
EI_VERIFY(0x00702D64, GetStatusVerify, 6, s_getStatus);
EI_VERIFY(0x0070D0D2, FileNamePart::Verify, 5, FileNamePart::original);
EI_VERIFY(0x00701AC9, FileTitlePart::Verify, 9, FileTitlePart::original);
