// The game's STLport vector<T> instantiations (stlp_vector.h) registered at their own addresses: the ones
// something still reaches by game.exe's address (the exit functions; tools/registration_readers.py, C20c). Our
// code calls the others directly. Instantiations with the same instructions (addresses masked) and element size
// share one body; the element size of each was read from its size arithmetic.
#include "stlp_vector.h"

#include "mfc.h"
#include "stlp.h"
#include "stlp_vector_refs.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

// Instantiations seen only through their element size.
using Vector1 = stlp::VectorAbi<char>;
using Vector4 = stlp::VectorAbi<std::uint32_t>;
using Vector16 = stlp::VectorAbi<stlp::Opaque<16>>;

} // namespace

// ~_Vector_base(): 32 instantiations, by element size (1 bytes: 7, 4 bytes: 21, 12 bytes: 1, 96 bytes: 1, 288 bytes: 1, 360 bytes: 1),
// and six ~vector() with it inlined that only exit functions call (S3c): of pointers 0056E800 (the
// frame lists 007AF508..007AF5A8, world.h), 0069BE20, 004A8C60, 004A8D00, 004A8D50 (the item
// stock's scratch vectors 007C8650, 007C8680, 007C8670, 007C8660) and of 16-byte entries 004A8CB0
// (its taken materials 007C8640).
EI_REPLACE(0x006B6D30, Vector1::DestroyBase);   // S16a: the sound voices' string (no Ghidra function)
EI_REPLACE(0x004E2A20, Vector4::DestroyBase);
EI_REPLACE(0x0056E800, Vector4::DestroyBase);
EI_REPLACE(0x0069BE20, Vector4::DestroyBase);
EI_REPLACE(0x004A8C60, Vector4::DestroyBase);
EI_REPLACE(0x004A8D00, Vector4::DestroyBase);
EI_REPLACE(0x004A8D50, Vector4::DestroyBase);
EI_REPLACE(0x004A8CB0, Vector16::DestroyBase);

// vector<stlp::string> (StringElement).
namespace {
using StringVector = stlp::VectorAbi<stlp::string, stlp::StringElement>;
} // namespace
EI_REPLACE(0x00421260, StringVector::Destruct);   // S3c: the quest files and folders 007AFDD0 / 007AFDC0 at exit
