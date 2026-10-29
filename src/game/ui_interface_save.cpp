// Interface3D's saved state. See ui.h.
#include "ui.h"

#include "core_streams.h"
#include "text_manager.h"

#include <cstdint>
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"
#include "save_versions.h"

#include <cstring>
#include <memory>

namespace {

// The save stream: reads `size` bytes; version is the save's version.
void Read(ByteStream* stream, void* to, int size) { stream->Read(to, static_cast<std::uint32_t>(size)); }
int Version(const ByteStream* stream) { return static_cast<int>(stream->version); }

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

void Interface3D::LoadState(ByteStream* stream) {
    stream->ReadValue(hudPanelsIn[0]);
    stream->ReadValue(hudPanelsIn[1]);
    stream->ReadValue(hudPanelsIn[2]);
    stream->ReadValue(hudBodyPart);
    stream->ReadValue(hudTopPage);
    if (Version(stream) < kVersionFastGame) return;
    stream->ReadValue(fastGame);
    if (Version(stream) < kVersionInterfaceList) return;
    // An obsolete list: each name and value read and dropped (the last name is kept until the end, like
    // the original's local string).
    std::string last;
    int count;
    stream->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        last = stream->ReadString();
        int value;
        stream->ReadValue(value);
    }
    messageText = stream->ReadString();
    if (Version(stream) >= kVersionMessageFlag) {
        stream->ReadValue(messageFlag);
        if (Version(stream) >= kVersionSavedText) savedText = stream->ReadString();
    }
}

// --- The multiplayer messages' texts -------------------------------------------------

char g_pauseMessageKey[12] = /*007930EC*/ {"reserved_02"};

std::string Interface3D::MessageText(int id, const char* value) {
    std::string key;
    if (static_cast<unsigned>(id) <= 12) key = kMessageKeys[id];
    key = g_clientWorld.texts->TextManager::GetString(key);
    if (value) return str::Format(key, value);
    return key;
}

// --- The saved state written -----------------------------------------------------------

void Interface3D::SaveState(ByteStream* stream) {
    for (std::uint8_t flag : hudPanelsIn) stream->WriteValue(flag);
    int value = hudBodyPart;
    stream->WriteValue(value);
    value = hudTopPage;
    stream->WriteValue(value);
    value = fastGame;
    stream->WriteValue(value);
    value = 0;   // the obsolete list LoadState skips: no entries
    stream->WriteValue(value);
    stream->WriteString(messageText);
    value = messageFlag;
    stream->WriteValue(value);
    stream->WriteString(savedText);
}
