// player_out_mark.h: marks in a player connection's outgoing messages.
#include "player_out_mark.h"

#include "fake_stream.h"

#include "../../src/game/player_messages.h"

#include <cstring>

static_assert(sizeof(PlayerOutMark::header) == sizeof(CMemoryStream));

PlayerOutMark PlayerOutTake(const void* server) {
    auto* p = static_cast<const CPlayerServer*>(server);
    PlayerOutMark mark;
    std::memcpy(mark.header, &p->out_, sizeof(mark.header));
    std::memcpy(mark.lastPosition, &p->lastPosition_, sizeof(mark.lastPosition));
    return mark;
}

bool PlayerOutSince(const void* server, const PlayerOutMark& mark, std::vector<std::uint8_t>* bytes) {
    auto* p = static_cast<const CPlayerServer*>(server);
    auto* before = reinterpret_cast<const CMemoryStream*>(mark.header);
    if (p->out_.data_ != before->data_ || p->out_.capacity_ != before->capacity_ || p->out_.size_ < before->size_)
        return false;
    bytes->assign(p->out_.data_ + before->size_, p->out_.data_ + p->out_.size_);
    return true;
}

void PlayerOutRestore(void* server, const PlayerOutMark& mark) {
    auto* p = static_cast<CPlayerServer*>(server);
    RawCopy(&p->out_, reinterpret_cast<const CMemoryStream*>(mark.header));
    std::memcpy(&p->lastPosition_, mark.lastPosition, sizeof(mark.lastPosition));
}
