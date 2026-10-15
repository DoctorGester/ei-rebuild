// CInterface3D's saved state (005F8420 reads it, 005F85C0 writes it). See ui.h.
#include "ui.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

namespace {

// The save stream (thiscall): slot 1 reads `size` bytes; +0x08 is the save's version.
void Read(void* stream, void* to, int size) {
    using Fn = void(__thiscall*)(void*, void*, int);
    reinterpret_cast<Fn>((*static_cast<void***>(stream))[1])(stream, to, size);
}
int Version(const void* stream) { return *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(stream) + 8); }


// A string read into a temporary (ReadString constructs it), assigned to `field`
// (CString::operator=, 00700AA9), then destroyed.
void ReadInto(void* stream, mfc::CString* field) {
    alignas(4) std::uint8_t storage[sizeof(mfc::CString)];
    auto* text = reinterpret_cast<mfc::CString*>(storage);
    *field = *StreamReadString(static_cast<CStream*>(stream), nullptr, text);
    text->~CString();
}

} // namespace

void CInterface3D::LoadState(void* stream) {
    Read(stream, &hudPanelsIn_[0], 1);
    Read(stream, &hudPanelsIn_[1], 1);
    Read(stream, &hudPanelsIn_[2], 1);
    Read(stream, &hudBodyPart_, 4);
    Read(stream, &hudTopPage_, 4);
    if (Version(stream) < 0x47) return;
    Read(stream, &fastGame_, 4);
    if (Version(stream) < 0x56) return;
    // An obsolete list: each name and value read and dropped (the last name is kept until the
    // end, as the original's local CString).
    mfc::CString last;
    int count;
    Read(stream, &count, 4);
    for (int i = 0; i < count; ++i) {
        ReadInto(stream, &last);
        int value;
        Read(stream, &value, 4);
    }
    ReadInto(stream, &messageText_);
    if (Version(stream) >= 0x5D) {
        Read(stream, &messageFlag_, 4);
        if (Version(stream) >= 0x5F) ReadInto(stream, &savedText_);
    }
}

// --- 005F8C90: the multiplayer messages' texts -------------------------------------------------

namespace {
// The text table (007AF788; thiscall): a key's text into `out` (004E0020, RET 8).

const std::uintptr_t kMessageKeys[13] = {
    0x007931DC,   // "lmp_player_connected"
    0x007931C4,   // "lmp_player_disconnected"
    0x007931B0,   // "lmp_player_kicked"
    0x0079319C,   // "lmp_player_banned"
    0x00793180,   // "lmp_player_lost_connection"
    0x00793170,   // "lmp_quest_taken"
    0x0079315C,   // "lmp_quest_canceled"
    0x00793148,   // "lmp_quest_completed"
    0x00793134,   // "lmp_zone_enabled"
    0x00793120,   // "lmp_zone_disabled"
    0x0079310C,   // "lmp_trade_requested"
    0x007930F8,   // "lmp_trade_canceled"
    0x007930EC,   // "reserved_02"
};
} // namespace

void* CInterface3D::MessageText(void* out, int id, int value) {
    mfc::CString key, text;
    if (static_cast<unsigned>(id) <= 12) key = reinterpret_cast<const char*>(kMessageKeys[id]);
    void* table = *reinterpret_cast<void* const*>(0x007AF788);
    TextsGetString(table, nullptr, key, &key);
    if (value != 0)
        text.Format(key, value);
    else
        text = key;
    CStringCopyConstructOriginal(out, nullptr, &text);
    return out;
}

EI_REPLACE_METHOD(0x005F8C90, CInterface3D::MessageText);

EI_REPLACE_METHOD(0x005F8420, CInterface3D::LoadState);

// --- 005F85C0: the saved state written -----------------------------------------------------------

void CInterface3D::SaveState(CStream* stream) {
    for (std::uint8_t flag : hudPanelsIn_) stream->Write(&flag, 1);
    int value = hudBodyPart_;
    stream->Write(&value, 4);
    value = hudTopPage_;
    stream->Write(&value, 4);
    value = fastGame_;
    stream->Write(&value, 4);
    value = 0;   // the obsolete list LoadState skips: no entries
    stream->Write(&value, 4);
    stream->Write(messageText_, static_cast<std::uint32_t>(std::strlen(messageText_) + 1));
    value = messageFlag_;
    stream->Write(&value, 4);
    stream->Write(savedText_, static_cast<std::uint32_t>(std::strlen(savedText_) + 1));
}
EI_REPLACE_METHOD(0x005F85C0, CInterface3D::SaveState);
