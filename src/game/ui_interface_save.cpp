// CInterface3D's saved state. See ui.h.
#include "ui.h"

#include "core_streams.h"
#include "item_db_text.h"

#include <cstdint>
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"

#include <cstring>
#include <memory>

namespace {

// The save stream: reads `size` bytes; version_ is the save's version.
void Read(CStream* stream, void* to, int size) { stream->Read(to, static_cast<std::uint32_t>(size)); }
int Version(const CStream* stream) { return static_cast<int>(stream->version_); }

// A string read into a temporary, assigned to `field`.
void ReadInto(CStream* stream, std::string* field) {
    std::string text;
    *field = stream->ReadString(&text)->c_str();
}

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
    g_pauseMessageKey,
};

} // namespace

void CInterface3D::LoadState(CStream* stream) {
    stream->ReadValue(hudPanelsIn_[0]);
    stream->ReadValue(hudPanelsIn_[1]);
    stream->ReadValue(hudPanelsIn_[2]);
    stream->ReadValue(hudBodyPart_);
    stream->ReadValue(hudTopPage_);
    if (Version(stream) < 0x47) return;
    stream->ReadValue(fastGame_);
    if (Version(stream) < 0x56) return;
    // An obsolete list: each name and value read and dropped (the last name is kept until the end, like
    // the original's local string).
    std::string last;
    int count;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        ReadInto(stream, &last);
        int value;
        stream->ReadValue(value);
    }
    ReadInto(stream, &messageText_);
    if (Version(stream) >= 0x5D) {
        stream->ReadValue(messageFlag_);
        if (Version(stream) >= 0x5F) ReadInto(stream, &savedText_);
    }
}

// --- The multiplayer messages' texts -------------------------------------------------

char g_pauseMessageKey[12] = /*007930EC*/ {"reserved_02"};

std::string* CInterface3D::MessageText(std::string* out, int id, const char* value) {
    std::string key, text;
    if (static_cast<unsigned>(id) <= 12) key = kMessageKeys[id];
    CTextManager* table = g_clientWorld->texts_;
    table->CTextManager::GetString(key, &key);
    if (value)
        text = str::Format(key, value);
    else
        text = key;
    *out = text; return out;
}

// --- The saved state written -----------------------------------------------------------

void CInterface3D::SaveState(CStream* stream) {
    for (std::uint8_t flag : hudPanelsIn_) stream->WriteValue(flag);
    int value = hudBodyPart_;
    stream->WriteValue(value);
    value = hudTopPage_;
    stream->WriteValue(value);
    value = fastGame_;
    stream->WriteValue(value);
    value = 0;   // the obsolete list LoadState skips: no entries
    stream->WriteValue(value);
    stream->WriteString(messageText_);
    value = messageFlag_;
    stream->WriteValue(value);
    stream->WriteString(savedText_);
}
