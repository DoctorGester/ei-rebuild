#pragma once

// MFC's TRY / CATCH, for the rewrites of the game functions that use it
// The game's MFC 4.2 is built with C++ exceptions: THROW is `throw` of a CException*, and
//     TRY { ... } CATCH(CFileException, e) { ... } END_CATCH
// is `{ AFX_EXCEPTION_LINK link; try { ... } catch (CFileException* e) { link.m_pException = e;
// ... } }`. The link's destructor (AfxTryCleanup) deletes the exception it was given and takes the
// link off the thread's chain, after the handler has run.
// Why that works across the two C++ runtimes: docs/notes/standalone.md, "The image's source files".
// It happens in the game when a missing file makes CStdioFile's constructor throw and the handler in
// script_text_file.cpp catches.

// The thrown and handler types (the names and bases are the game's: see above).
class CObject {};
class CException : public CObject {};
class CSimpleException : public CException {};
class CFileException : public CException {};
class CArchiveException : public CException {};
class CNotSupportedException : public CSimpleException {};
class CMemoryException : public CSimpleException {};
class CResourceException : public CSimpleException {};

namespace mfc {

// AFX_EXCEPTION_LINK: one TRY block on the thread's chain of them.
struct ExceptionLink {
    ExceptionLink();    // no exception, put on the chain
    ~ExceptionLink();   // AfxTryCleanup: the exception released, the top link taken off
    ExceptionLink(const ExceptionLink&) = delete;
    ExceptionLink& operator=(const ExceptionLink&) = delete;

    ExceptionLink* previous;
    void* exception;           // m_pException: what the handler caught (deleted with the link)
};
static_assert(sizeof(ExceptionLink) == 8);

} // namespace mfc
