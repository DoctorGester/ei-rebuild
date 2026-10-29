// The game client's timed entries and commands: an object kept on the timers list, a
// command message for the server, the client's own server entry, the start of the game and the ratio of a
// received buffer. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"
#include "core_refcount.h"
#include "net_coder.h"

#include "world.h"
#include "game_singletons.h"

#include "lib/rounded_quotient.h"

#include <cstring>


namespace net {

namespace {

constexpr std::uint32_t kCommandLifetime = 0x42700000;     // 60.0f


} // namespace

// Keeps `object` on the timers list for `seconds` (shared into the entry).
void ClientSession::AddTimed(RefObject* object, std::uint32_t seconds) {
    // The list's copy shares the entry's share: a clone, as sharing marked the object copy-on-share.
    TimedEntry<SharedRef<RefObject>> entry(seconds, SharedRef<RefObject>(object));
    timers.push_back(entry);
}

// Sends the server a command message, kept 60 seconds on the timers list.
void ClientSession::SendCommand(int kind, std::string_view text) {
    ClientSession* self = this;
    auto* command = NewCounted<CommandMessage>();
    command->kind = static_cast<std::uint32_t>(kind);
    // The assign of a C string: copied over the current characters, the rest cut (the
    // terminator moved down) or appended.
    command->text.assign(text);

    // As AddTimed: the list keeps a clone of the entry's share.
    TimedEntry<SharedRef<RefObject>> entry(kCommandLifetime, SharedRef<RefObject>(command));
    self->timers.push_back(entry);
}

// Makes `*out` an empty server entry (no name or description, kind 4, no players).
void ClientSession::CreateEntry(Ref<ServerEntry>* out) { ShareAssignOwned(*out, NewCounted<ServerEntry>()); }

// The game flow begins the multiplayer game.
void ClientSession::OnJoined() { g_gameFlow.BeginMultiplayerGame(); }

// Called by the transfer screen: a buffer's stream size (its base plus the highest written position, from
// `begin`) over the dword at its start (1 when 0), both as signed integers; 0 for fewer than 4 bytes. Reading
// the dword moves the read position to 4 (a Seek(0), then a 4-byte read as BufferRead does it).
// The quotient rounded once to a float, as the original's division at 24-bit precision (the sizes can pass 2^24).
float ClientSession::BufferRatio(GameBuffer* buffer) {
    auto streamSize = [buffer] {
        if (buffer->cur > buffer->high) buffer->high = buffer->cur;
        return buffer->PositionOf(buffer->high);
    };
    if (streamSize() < 4) {
        return 0.0f;
    }
    buffer->Seek(0);
    std::uint32_t stored;
    buffer->ReadBytes(&stored, 4);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return lib::RoundedQuotient(streamSize(), divisor);
}

} // namespace net
