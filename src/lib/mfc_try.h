#pragma once

// MFC's TRY / CATCH, for the rewrites of the game functions that use it (A-E2).
//
// The game's MFC 4.2 is built with C++ exceptions: THROW is `throw` of a CException*, and
//
//     TRY { ... } CATCH(CFileException, e) { ... } END_CATCH
//
// is `{ AFX_EXCEPTION_LINK link; try { ... } catch (CFileException* e) { link.m_pException = e;
// ... } }`. The link's destructor (AfxTryCleanup) deletes the exception it was given and takes the
// link off the thread's chain, after the handler has run.
//
// A rewrite writes exactly that. What makes it work across the two C++ runtimes:
// - a throw is `throw` of a pointer to one of the classes below (C18e): this compiler makes the throw
//   information, which lists the pointer to the class and to each base like the game's did
//   (".PAVCFileException@@", ".PAVCException@@", ".PAVCObject@@", ".PAX");
// - a handler matches a thrown object when the type descriptors are the same object or carry the
//   same decorated name. So the classes are in the global namespace under the game's names and have
//   its bases; nothing else of them is used (they are never made here: the objects are the game's
//   MFC objects, thrown through a cast pointer);
// - the unwind runs the frames between the throw and the handler through their frame handlers
//   (RtlUnwind), then ours.
// verify_mfc_try.cpp proved it at every start until S21d (a missing file: the game's CStdioFile
// constructor throws, the handler in script_text_file.cpp catches).

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
    ExceptionLink();    // 007020F1: no exception, put on the chain
    ~ExceptionLink();   // AfxTryCleanup (0070212F): the exception released, the top link taken off
    ExceptionLink(const ExceptionLink&) = delete;
    ExceptionLink& operator=(const ExceptionLink&) = delete;

    ExceptionLink* previous;   // +0x00 m_pLinkPrev
    void* exception;           // +0x04 m_pException: what the handler caught (deleted with the link)
};
static_assert(sizeof(ExceptionLink) == 8);

} // namespace mfc
