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
// - the game throws through its own _CxxThrowException (006E7819): RaiseException(0xE06D7363) with
//   the magic 0x19930520 and the throw information, the layout this compiler's frame handler still
//   reads (x86);
// - a handler matches a thrown object when the type descriptors are the same object or carry the
//   same decorated name, and the game's throw information lists the pointer to the class and to
//   each base (".PAVCFileException@@", ".PAVCException@@", ".PAVCObject@@", ".PAX"). So a handler
//   here must name a class with the game's name in the global namespace; nothing else of the class
//   is used (the two below are never made here);
// - the unwind runs the game's frames between the throw and the handler through their own frame
//   handlers (RtlUnwind), then ours.
// verify_mfc_try.cpp proved it at every start until S21d (a missing file: the game's CStdioFile
// constructor throws, the handler in script_text_file.cpp catches).

// The handler types (the names are the game's: see above).
class CException {
    CException() = delete;
};
class CFileException {
    CFileException() = delete;
};

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
