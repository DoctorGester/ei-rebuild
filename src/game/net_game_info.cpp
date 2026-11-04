// The game announcement record (GameInfoRecord). See net_classes.h and docs/notes/network.md.
#include "net_classes.h"

#include "net_coder.h"

namespace net {

namespace {

// Control characters (below 0x20) of a received string become '|'.
void Sanitize(std::string* s) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s->size()); ++i) {
        if (static_cast<std::uint8_t>((*s)[i]) < 0x20) (*s)[i] = '|';
    }
}

} // namespace

void GameInfoRecord::Write(GameBuffer* buffer) const {
    buffer->Put(masterKey);
    buffer->Put(masterToken);
    buffer->WriteString(&name);
    buffer->WriteString(&description);
    buffer->Put(players);
    buffer->Put(maxPlayers);
    buffer->Put(hasPassword);
    buffer->Put(kind);
    buffer->Put(magic);
}

// Reads the record in the same order (strings of at most 0x100 bytes), replaces control
// characters in both strings with '|'; returns 1 unless the buffer ran short.
int GameInfoRecord::Read(GameBuffer* buffer) {
    buffer->Get(masterKey);
    buffer->Get(masterToken);
    buffer->ReadString(&name, 0x100);
    buffer->ReadString(&description, 0x100);
    buffer->Get(players);
    buffer->Get(maxPlayers);
    buffer->Get(hasPassword);
    buffer->Get(kind);
    buffer->Get(magic);
    Sanitize(&name);
    Sanitize(&description);
    return buffer->readPastEnd ? 0 : 1;
}

} // namespace net
