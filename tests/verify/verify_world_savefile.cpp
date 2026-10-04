// Checks for the save files (src/game/world_savefile.cpp). They write and read files, so only
// ours runs; the writer is checked by LoadSave reading the files back (verify_world_save.cpp),
// the reader by its result.
#include "verify.h"

#include "../../src/game/world.h"

#include "ei/vcall.h"

namespace {
void* s_unused;
int StreamSize(void* stream) { return ei::VCall<int>(stream, 4); }
} // namespace

// 00579B80: logged with the data size.
static void __fastcall SaveFileWriteVerify(void* file, unsigned magic, int version, void* data) {
    int size = StreamSize(data);
    SaveFileWrite(file, magic, version, data);
    ei::verify::Check("SaveFileWrite", true, true, "magic %X version %d, %d bytes", magic, version, size);
}
EI_VERIFY(0x00579B80, SaveFileWriteVerify, 0, s_unused);

// 00579830: a file that reads has data and takes the file's version.
static bool __fastcall SaveFileReadVerify(void* out, unsigned magic, int minimum, int maximum, void* file) {
    bool result = SaveFileRead(out, magic, minimum, maximum, file);
    int outVersion = *reinterpret_cast<int*>(static_cast<std::uint8_t*>(out) + 8);
    int fileVersion = *reinterpret_cast<int*>(static_cast<std::uint8_t*>(file) + 8);
    bool ok = !result || (outVersion == fileVersion && StreamSize(out) > 0);
    ei::verify::Check("SaveFileRead", true, ok, "magic %X -> %d, version %d, %d bytes", magic, result, fileVersion,
                      result ? StreamSize(out) : 0);
    return result;
}
EI_VERIFY(0x00579830, SaveFileReadVerify, 0, s_unused);
