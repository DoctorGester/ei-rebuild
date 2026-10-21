// The save game's difference coding between two streams (A-S4): CStream::WriteDiff (004A4DA0)
// and CStream::ApplyDiff (004A50D0). The format is described in core_streams.h. Every stream
// access goes through the stream's virtual functions, in the original's order.
#include "core_streams.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

constexpr int kFullRun = 0x100;

void PutByte(CStream* stream, std::uint8_t value) { stream->Write(&value, 1); }

// Writes `length` into the run header at `header`, comes back to the end and writes the next run's
// header (0 for now); returns where that header is.
std::uint32_t CloseRun(CStream* out, std::uint32_t header, std::uint8_t length) {
    std::uint32_t end = out->Position();
    out->Seek(header);
    PutByte(out, length);
    out->Seek(end);
    std::uint32_t next = out->Position();
    PutByte(out, 0);
    return next;
}

} // namespace

void CStream::WriteDiff(CStream* base, CStream* current) {
    if (base->Size() != current->Size()) return;
    base->Seek(0);
    current->Seek(0);
    int remaining = static_cast<int>(base->Size());
    bool equalRun = true;
    int count = 0;
    std::uint32_t header = Position();
    PutByte(this, 0);
    for (; remaining > 0; --remaining) {
        std::uint8_t was;
        std::uint8_t now;
        base->Read(&was, 1);
        current->Read(&now, 1);
        if (was != now) {
            if (!equalRun) {
                if (++count == kFullRun) {
                    header = CloseRun(this, header, 0xFF);
                    header = CloseRun(this, header, 0);   // an empty run of equal bytes
                    count = 1;
                }
            } else {
                header = CloseRun(this, header, static_cast<std::uint8_t>(count));
                equalRun = false;
                count = 1;
            }
            PutByte(this, now);
        } else if (equalRun) {
            if (++count == kFullRun) {
                header = CloseRun(this, header, 0xFF);
                header = CloseRun(this, header, 0);       // an empty run of changed bytes
                count = 1;
            }
        } else {
            header = CloseRun(this, header, static_cast<std::uint8_t>(count));
            equalRun = true;
            count = 1;
        }
    }
    std::uint32_t end = Position();
    Seek(header);
    PutByte(this, static_cast<std::uint8_t>(count));
    Seek(end);
    Position();
    PutByte(this, 0);
}
EI_REWRITTEN(0x004A4DA0, CStream::WriteDiff);

void CStream::ApplyDiff(CStream* base, CStream* diff) {
    std::uint32_t length = 0;   // one byte is read into it at a time
    int done = 0;
    const int size = static_cast<int>(base->Size());
    base->Seek(0);
    Seek(0);
    if (size <= 0) return;
    std::uint8_t buffer[0x100];
    do {
        diff->Read(&length, 1);
        base->Read(buffer, length);
        Write(buffer, length);
        done += static_cast<int>(length);
        if (!diff->Read(&length, 1)) return;
        base->Read(buffer, length);
        diff->Read(buffer, length);
        Write(buffer, length);
        done += static_cast<int>(length);
    } while (done < size);
}
EI_REWRITTEN(0x004A50D0, CStream::ApplyDiff);
