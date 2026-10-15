// Checks for MFC's exception classes and AFX_EXCEPTION_LINK (src/lib/mfc_exception.cpp, ticket S8).
// OsErrorToException, CException's constructor and the GetRuntimeClass slots are pure: against the
// original through the trampoline (OsErrorToException also for every code up to 0x1000 at start-up).
// The throws, Delete, the destructors and the link run ours only: the wrappers check the arguments and
// the state they leave, and a start-up check throws each kind through its address and catches it
// here as the game's handlers would (by the class's name), then reads the object it caught.
#include "verify.h"

#include "../../src/lib/mfc_exception.h"

#include <cstring>

// Handler types named as the game's classes (mfc_try.h declares CException and CFileException).
class CArchiveException {
    CArchiveException() = delete;
};
class CNotSupportedException {
    CNotSupportedException() = delete;
};

#pragma warning(disable : 4740) // naked forwarders

namespace {

// The library's objects (the global names are the handler types).
using mfc::impl::CArchiveException;
using mfc::impl::CException;
using mfc::impl::CFileException;
using mfc::impl::CSimpleException;
using ei::verify::Check;
void* s_unused;

// The functions through their addresses (the wrappers below once the hooks are installed).
__declspec(naked) void __stdcall ThrowFileExceptionAt(int, long, const char*) EI_ORIGINAL(0x007031D5)
__declspec(naked) void __stdcall ThrowOsErrorAt(long, const char*) EI_ORIGINAL(0x007030F0)
__declspec(naked) void __stdcall ThrowArchiveExceptionAt(int, const char*) EI_ORIGINAL(0x0070A835)
__declspec(naked) void __stdcall ThrowNotSupportedAt() EI_ORIGINAL(0x006FE3A2)
__declspec(naked) void __fastcall DeleteExceptionAt(void*) EI_ORIGINAL(0x00702069)
// The thread's chain (the original's thread state in the hook build, ours in the image).
__declspec(naked) mfc::ExceptionLink** __cdecl LinkTopAt() EI_ORIGINAL(0x00702107)

const char* Text(const mfc::impl::CString& s) { return static_cast<const char*>(s); }

// ---- Pure functions --------------------------------------------------------------------------------

// 00703296: MOV EAX,[ESP+4]; MOV ECX,0x10B (9 bytes).
int(__stdcall* s_osErrorToException)(long);
int __stdcall OsErrorToExceptionVerify(long osError) {
    const int expected = s_osErrorToException(osError);
    const int actual = mfc::impl::OsErrorToException(osError);
    Check("CFileException::OsErrorToException", expected, actual, "error %ld", osError);
    return actual;
}

// 00702053: MOV EAX,ECX; MOV dword ptr [EAX+4],1 (9 bytes).
CException*(__fastcall* s_construct)(CException*);
CException* __fastcall ConstructVerify(CException* self) {
    CException twin;
    std::memcpy(&twin, self, sizeof(twin));
    CException* expected = s_construct(&twin);
    CException* actual = self->Construct();
    Check("CException::CException", true, expected == &twin && actual == self && std::memcmp(&twin, self, sizeof(twin)) == 0,
          "auto-delete %d (original %d)", self->autoDelete, twin.autoDelete);
    return actual;
}

// GetRuntimeClass: MOV EAX,imm32 (5 bytes).
template <int Id>
struct RuntimeClass {
    static inline void*(__fastcall* original)(const void*);
    static inline void*(__fastcall* ours)(const void*);
    static inline const char* name;
    static void* __fastcall Verify(const void* self) {
        void* expected = original(self);
        void* actual = ours(self);
        Check(name, expected, actual, "");
        return actual;
    }
};
using FileExceptionClass = RuntimeClass<0>;
using ArchiveExceptionClass = RuntimeClass<1>;

// ---- Ours only -------------------------------------------------------------------------------------

void __stdcall ThrowFileExceptionVerify(int cause, long osError, const char* fileName) {
    Check("AfxThrowFileException", true, cause >= CFileException::none && cause <= CFileException::endOfFile, "cause %d, error %ld",
          cause, osError);
    mfc::impl::ThrowFileException(cause, osError, fileName);
}

void __stdcall ThrowOsErrorVerify(long osError, const char* fileName) {
    Check("CFileException::ThrowOsError", true, true, "error %ld", osError);
    mfc::impl::ThrowOsError(osError, fileName);
}

void __stdcall ThrowArchiveExceptionVerify(int cause, const char* fileName) {
    Check("AfxThrowArchiveException", true, cause >= CArchiveException::none && cause <= CArchiveException::badSchema, "cause %d",
          cause);
    mfc::impl::ThrowArchiveException(cause, fileName);
}

void __stdcall ThrowNotSupportedVerify() {
    Check("AfxThrowNotSupportedException", true, true, "");
    mfc::impl::ThrowNotSupported();
}

void __fastcall DeleteExceptionVerify(CException* exception) {
    Check("CException::Delete", true, exception != nullptr, "");
    mfc::impl::DeleteException(exception);
}

void __fastcall FileExceptionDestructVerify(CFileException* self) {
    self->Destruct();
    Check("CFileException::~CFileException", true, self->vtable == CFileException::Vtable(), "");
}

CFileException* __fastcall FileExceptionDeleteVerify(CFileException* self, void*, unsigned flags) {
    CFileException* result = self->DeletingDestructor(flags);
    Check("CFileException deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

void __fastcall ArchiveExceptionDestructVerify(CArchiveException* self) {
    self->Destruct();
    Check("CArchiveException::~CArchiveException", true, self->vtable == CArchiveException::Vtable(), "");
}

CArchiveException* __fastcall ArchiveExceptionDeleteVerify(CArchiveException* self, void*, unsigned flags) {
    CArchiveException* result = self->DeletingDestructor(flags);
    Check("CArchiveException deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

// The link goes on top of the thread's chain; the cleanup takes the top one off.
mfc::ExceptionLink* __fastcall ConstructLinkVerify(mfc::ExceptionLink* link) {
    mfc::ExceptionLink* result = mfc::ConstructExceptionLink(link);
    Check("AFX_EXCEPTION_LINK::AFX_EXCEPTION_LINK", true, result == link && link->exception == nullptr, "");
    return result;
}

void __cdecl TryCleanupVerify() {
    mfc::ExceptionLink** top = LinkTopAt();
    mfc::ExceptionLink* link = *top;
    mfc::ExceptionLink* below = link ? link->previous : nullptr;
    Check("AfxTryCleanup", true, link != nullptr, "no link on the chain");
    mfc::TryCleanup();
    Check("AfxTryCleanup", true, *top == below, "top %p, expected %p", *top, below);
}

// ---- Start-up check --------------------------------------------------------------------------------

void OsErrorTableCheck() {
    int mismatches = 0;
    long first = -1;
    for (long code = 0; code <= 0x1000; ++code) {
        if (s_osErrorToException(code) != mfc::impl::OsErrorToException(code)) {
            if (first < 0) first = code;
            ++mismatches;
        }
    }
    for (long code : {-1L, 0x7FFFFFFFL, static_cast<long>(0x80070005L), 0x10000L}) {
        if (s_osErrorToException(code) != mfc::impl::OsErrorToException(code)) ++mismatches;
    }
    Check("CFileException::OsErrorToException (all codes)", 0, mismatches, "first at %ld", first);
}

const mfc::ExceptionLink* ChainTop() { return *LinkTopAt(); }

void ThrowCheck() {
    const mfc::ExceptionLink* top = ChainTop();
    // A file exception: its fields, deleted through Delete.
    {
        bool caught = false;
        try {
            ThrowFileExceptionAt(CFileException::accessDenied, 5, "x:\\missing\\file.txt");
        } catch (::CFileException* thrown) {
            auto* e = reinterpret_cast<CFileException*>(thrown);
            caught = e->vtable == CFileException::Vtable() && e->autoDelete == 1 && e->cause == CFileException::accessDenied &&
                     e->osError == 5 && std::strcmp(Text(e->fileName), "x:\\missing\\file.txt") == 0;
            DeleteExceptionAt(e);
        }
        Check("AfxThrowFileException (caught)", true, caught, "");
    }
    // ThrowOsError: nothing for 0; the code's cause otherwise.
    {
        bool returned = false;
        try {
            ThrowOsErrorAt(0, nullptr);
            returned = true;
        } catch (::CException*) {
        }
        bool caught = false;
        try {
            ThrowOsErrorAt(ERROR_SHARING_VIOLATION, "name");
        } catch (::CFileException* thrown) {
            auto* e = reinterpret_cast<CFileException*>(thrown);
            caught = e->cause == CFileException::sharingViolation && e->osError == ERROR_SHARING_VIOLATION &&
                     std::strcmp(Text(e->fileName), "name") == 0;
            DeleteExceptionAt(e);
        }
        Check("CFileException::ThrowOsError (caught)", true, returned && caught, "returned %d, caught %d", returned, caught);
    }
    // An archive exception, caught as its base class.
    {
        bool caught = false;
        try {
            ThrowArchiveExceptionAt(CArchiveException::endOfFile, nullptr);
        } catch (::CException* thrown) {
            auto* e = reinterpret_cast<CArchiveException*>(thrown);
            caught = e->vtable == CArchiveException::Vtable() && e->autoDelete == 1 && e->cause == CArchiveException::endOfFile &&
                     e->fileName.GetLength() == 0;
            DeleteExceptionAt(e);
        }
        Check("AfxThrowArchiveException (caught)", true, caught, "");
    }
    // The static not-supported exception (not auto-deleted: Delete leaves it).
    {
        bool caught = false;
        try {
            ThrowNotSupportedAt();
        } catch (::CNotSupportedException* thrown) {
            auto* e = reinterpret_cast<CException*>(thrown);
            caught = e->autoDelete == 0;
            DeleteExceptionAt(e);
        }
        Check("AfxThrowNotSupportedException (caught)", true, caught, "");
    }
    Check("exception link chain (after the throws)", true, ChainTop() == top, "%p / %p", ChainTop(), top);
}

// The four static exceptions' constructors (S16): the original builds a scratch object, ours
// another from the same bytes; every byte and the returned this compared.
using SimpleConstructFn = CSimpleException*(__fastcall*)(CSimpleException*, void*, BOOL, unsigned);
template <std::uintptr_t Address>
struct SimpleExceptionConstruct {
    static inline SimpleConstructFn original;
    static inline SimpleConstructFn ours;
    static inline const char* name;
    static CSimpleException* __fastcall Verify(CSimpleException* self, void*, BOOL autoDelete, unsigned id) {
        CSimpleException theirs;
        std::memcpy(&theirs, self, sizeof(theirs));
        CSimpleException* expected = original(&theirs, nullptr, autoDelete, id);
        CSimpleException* actual = ours(self, nullptr, autoDelete, id);
        Check(name, true, expected == &theirs && actual == self && std::memcmp(&theirs, self, sizeof(theirs)) == 0,
              "%d, %04X", autoDelete, id);
        return actual;
    }
};
using ResourceExceptionConstruct = SimpleExceptionConstruct<0x006E68C9>;
using UserExceptionConstruct = SimpleExceptionConstruct<0x006E6916>;
using MemoryExceptionConstruct = SimpleExceptionConstruct<0x006E6AEA>;
using NotSupportedExceptionConstruct = SimpleExceptionConstruct<0x006E6B37>;

// Only the static initialisers call them (before the hooks): each wrapper once on a scratch object,
// and ours against MFC's static object the original built (its fields as the constructor left them).
template <typename C>
void SimpleExceptionCheck(const CSimpleException* global, BOOL autoDelete, unsigned id) {
    CSimpleException scratch;
    std::memset(&scratch, 0xCD, sizeof(scratch));
    C::Verify(&scratch, nullptr, autoDelete, id);
    bool ok = global->vtable == scratch.vtable && global->autoDelete == scratch.autoDelete &&
              global->initialized == scratch.initialized && global->loaded == scratch.loaded &&
              global->resourceId == scratch.resourceId;
    Check(C::name, true, ok, "the static object at %p", static_cast<const void*>(global));
}

void SimpleExceptionsCheck() {
    auto at = [](std::uintptr_t a) { return reinterpret_cast<const CSimpleException*>(a); };
    SimpleExceptionCheck<ResourceExceptionConstruct>(at(0x007C8C58), 0, 0xF022);
    SimpleExceptionCheck<UserExceptionConstruct>(at(0x007C8BC0), 0, 0xF024);
    SimpleExceptionCheck<MemoryExceptionConstruct>(at(0x007C8DE8), 0, 0xF023);
    SimpleExceptionCheck<NotSupportedExceptionConstruct>(at(0x007C8D50), 0, 0xF021);
}

void ExceptionStartupCheck() {
    OsErrorTableCheck();
    ThrowCheck();
    SimpleExceptionsCheck();
}
EI_STARTUP_CHECK(ExceptionStartupCheck);

struct Names {
    Names() {
        FileExceptionClass::name = "CFileException::GetRuntimeClass";
        FileExceptionClass::ours = &mfc::impl::FileExceptionRuntimeClass;
        ArchiveExceptionClass::name = "CArchiveException::GetRuntimeClass";
        ArchiveExceptionClass::ours = &mfc::impl::ArchiveExceptionRuntimeClass;
        ResourceExceptionConstruct::name = "CResourceException::CResourceException";
        ResourceExceptionConstruct::ours = &mfc::impl::ConstructResourceException;
        UserExceptionConstruct::name = "CUserException::CUserException";
        UserExceptionConstruct::ours = &mfc::impl::ConstructUserException;
        MemoryExceptionConstruct::name = "CMemoryException::CMemoryException";
        MemoryExceptionConstruct::ours = &mfc::impl::ConstructMemoryException;
        NotSupportedExceptionConstruct::name = "CNotSupportedException::CNotSupportedException";
        NotSupportedExceptionConstruct::ours = &mfc::impl::ConstructNotSupportedException;
    }
} s_names;

} // namespace

EI_VERIFY(0x00703296, OsErrorToExceptionVerify, 9, s_osErrorToException);
EI_VERIFY(0x00702053, ConstructVerify, 9, s_construct);
EI_VERIFY(0x0070CF4C, FileExceptionClass::Verify, 5, FileExceptionClass::original);
EI_VERIFY(0x0070CF46, ArchiveExceptionClass::Verify, 5, ArchiveExceptionClass::original);
EI_VERIFY(0x007031D5, ThrowFileExceptionVerify, 0, s_unused);
EI_VERIFY(0x007030F0, ThrowOsErrorVerify, 0, s_unused);
EI_VERIFY(0x0070A835, ThrowArchiveExceptionVerify, 0, s_unused);
EI_VERIFY(0x006FE3A2, ThrowNotSupportedVerify, 0, s_unused);
EI_VERIFY(0x00702069, DeleteExceptionVerify, 0, s_unused);
// PUSH ESI; MOV ESI,ECX; PUSH [ESP+8] (7).
EI_VERIFY(0x006E68C9, ResourceExceptionConstruct::Verify, 7, ResourceExceptionConstruct::original);
EI_VERIFY(0x006E6916, UserExceptionConstruct::Verify, 7, UserExceptionConstruct::original);
EI_VERIFY(0x006E6AEA, MemoryExceptionConstruct::Verify, 7, MemoryExceptionConstruct::original);
EI_VERIFY(0x006E6B37, NotSupportedExceptionConstruct::Verify, 7, NotSupportedExceptionConstruct::original);
EI_VERIFY(0x006E6335, FileExceptionDestructVerify, 0, s_unused);
EI_VERIFY(0x006E6319, FileExceptionDeleteVerify, 0, s_unused);
EI_VERIFY(0x006E6BA1, ArchiveExceptionDestructVerify, 0, s_unused);
EI_VERIFY(0x006E6B85, ArchiveExceptionDeleteVerify, 0, s_unused);
EI_VERIFY(0x007020F1, ConstructLinkVerify, 0, s_unused);
EI_VERIFY(0x0070212F, TryCleanupVerify, 0, s_unused);
