// The save files (scenario.sav, mission.sav, saves\current\<zone>.sav): the writer puts a memory stream into a
// file stream under a header, LZ-compressed (and checked by decompressing it again); the reader reads one back.
// See core/coder.h for the coder and docs/notes/world.md.
#include "game/saves/save_file.h"

#include "engine/core/core_streams.h"
#include "engine/core/coder.h"
#include "game/world/hoover.h"
#include "game/saves/save_versions.h"
#include "lib/bytes.h"

#include <cstring>

namespace {

std::uint32_t Checksum(const std::uint8_t* data, std::int32_t size) {
    std::uint32_t h = 0;
    for (std::int32_t at = 0; at < (size >> 2) * 4; at += 4) {
        std::uint32_t word = lib::Load<std::uint32_t>(data + at);
        h ^= word;
        h *= (h & 0xFC) | 3;
    }
    return h;
}

} // namespace

void SaveFileWrite(ByteStream* file, unsigned magic, MemoryStream* data) {
    CoderState coder;
    std::uint8_t* begin = data->data.data();
    std::uint8_t* last = begin + data->Size();
    BufferView view(begin, last);
    {
        GrowingBuffer packed;
        std::uint8_t compressed = 1;
        coder.Compress(&view, &packed);
        file->WriteHeader(magic, kSaveVersion);
        std::uint32_t sum = Checksum(data->data.data(), static_cast<std::int32_t>(data->Size()));
        file->WriteValue(sum);
        // Decompressed again; the packed data is used only when it gives the input back.
        GrowingBuffer check;
        packed.Seek(0);
        {
            CoderState decoder;
            decoder.Decompress(&packed, &check);
            std::int32_t size = static_cast<std::int32_t>(data->Size());
            bool same = check.Size() == size;
            if (same) {
                std::uint8_t* original = data->data.data();
                same = std::memcmp(original, check.begin, static_cast<std::size_t>(data->Size())) == 0;
            }
            if (same) {
                file->WriteValue(compressed);
                file->Write(packed.begin, static_cast<std::uint32_t>(packed.Size()));
            } else {
                compressed = 0;
                file->WriteValue(compressed);
                std::uint8_t* raw = data->data.data();
                file->Write(raw, data->Size());
            }
        }   // the decoder's cursor ends here, then `check` and `packed` go
    }
}

bool SaveFileRead(MemoryStream* out, unsigned magic, ByteStream* file) {
    if (!file->ReadHeader(magic, kSaveVersion, kSaveOldestVersion)) return false;
    if (file->version <= kSaveLastUncompressedVersion) {
        int start = static_cast<int>(file->Position());
        int size = static_cast<int>(file->Size()) - start;
        out->Resize(static_cast<std::uint32_t>(size));
        file->Read(out->data.data(), static_cast<std::uint32_t>(size));
        out->Seek(0);
    } else {
        std::uint8_t compressed = 1;
        CoderState coder;
        GrowingBuffer packed;
        GrowingBuffer plain;
        std::uint32_t stored = 0;
        if (file->version >= kVersionChecksum) file->ReadValue(stored);
        if (file->version >= kVersionCompressedFlag) file->ReadValue(compressed);
        int start = static_cast<int>(file->Position());
        int size = static_cast<int>(file->Size()) - start;
        if (compressed) {
            packed.Seek(size);
            file->Read(packed.begin, static_cast<std::uint32_t>(size));
            packed.Seek(0);
            coder.Decompress(&packed, &plain);
            if (file->version >= kVersionChecksum && Checksum(plain.begin, plain.Size()) != stored)
                return false;   // the buffers and the coder's cursor go on the way out
        } else {
            plain.Seek(size);
            file->Read(plain.begin, static_cast<std::uint32_t>(size));
        }
        const auto length = static_cast<std::uint32_t>(plain.Size());
        out->Resize(length);
        out->Seek(0);
        out->Write(plain.begin, length);
        out->Seek(0);
    }
    out->version = file->version;
    return true;
}
