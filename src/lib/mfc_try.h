#pragma once

// The types MFC's exceptions are thrown as. The game's MFC 4.2 is built with C++ exceptions: THROW is `throw` of a
// CException*, which a handler catches by the class's name. Why that works across the two C++ runtimes:
// docs/notes/standalone.md, "The image's source files".

// The thrown and handler types (the names and bases are the game's: see above).
class CObject {};
class CException : public CObject {};
class CSimpleException : public CException {};

class CMemoryException : public CSimpleException {};
class CResourceException : public CSimpleException {};
