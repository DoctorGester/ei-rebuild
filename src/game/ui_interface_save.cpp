// CInterface3D's saved state (005F8420 reads it, 005F85C0 writes it). See ui.h.
#include "ui.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

// The save stream: slot 1 reads `size` bytes; version_ is the save's version.
void Read(CStream* stream, void* to, int size) { stream->Read(to, static_cast<std::uint32_t>(size)); }
int Version(const CStream* stream) { return static_cast<int>(stream->version_); }

// A string read into a temporary (ReadString constructs it), assigned to `field`
// (CString::operator=, 00700AA9), then destroyed.
void ReadInto(CStream* stream, mfc::CString* field) {
    alignas(4) std::uint8_t storage[sizeof(mfc::CString)];
    auto* text = reinterpret_cast<mfc::CString*>(storage);
    *field = *stream->ReadString(text);
    text->~CString();
}

} // namespace

void CInterface3D::LoadState(CStream* stream) {
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

const char* const kMessageKeys[13] = {
    "lmp_player_connected",
    "lmp_player_disconnected",
    "lmp_player_kicked",
    "lmp_player_banned",
    "lmp_player_lost_connection",
    "lmp_quest_taken",
    "lmp_quest_canceled",
    "lmp_quest_completed",
    "lmp_zone_enabled",
    "lmp_zone_disabled",
    "lmp_trade_requested",
    "lmp_trade_canceled",
    "reserved_02",
};
} // namespace

void* CInterface3D::MessageText(void* out, int id, const char* value) {
    mfc::CString key, text;
    if (static_cast<unsigned>(id) <= 12) key = kMessageKeys[id];
    void* table = TheTextManager();
    TextsGetString(table, nullptr, key, &key);
    if (value)
        text.Format(key, value);
    else
        text = key;
    new (out) mfc::CString(text);
    return out;
}

EI_REWRITTEN(0x005F8C90, CInterface3D::MessageText);

EI_REWRITTEN(0x005F8420, CInterface3D::LoadState);

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
EI_REWRITTEN(0x005F85C0, CInterface3D::SaveState);
