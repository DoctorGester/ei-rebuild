// ei_vectors.exe links our game sources without the hook framework and without game.exe.
#include "ei/hook.h"
#include "vectors.h"

namespace ei {

// ei_vectors runs without game.exe: code that would write the exe's own runtime data in the hook build
// (the game CRT's errno, libcrt::WideToBytes) leaves it alone, as in the standalone image.
bool Standalone() { return true; }

} // namespace ei
