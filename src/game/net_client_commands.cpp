// The game client's timed entries and commands: an object kept on the timers list (0041D1A0), a
// command message for the server (0041D260), the client's own server entry (0041D420, slot 16), the
// start of the game (0041D410, slot 15) and the ratio of a received buffer (0041D9A0).
// See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"
#include "game_strings.h"
#include "core_refcount.h"
#include "net_coder.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>


namespace net {

namespace {

constexpr std::uint32_t kCommandLifetime = 0x42700000;     // 60.0f

// RefPtr::Destroy as the callers inline it (the release is not null-checked after the shutdown,
// and done inline: one reference less, deleted at zero).
void DestroyInline(RefPtr* p) {
    CRefObject* object = p->ptr;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        ++object->refs_;
        object->OnShutdown();
        --object->refs_;
    }
    CRefObject* held = p->ptr;
    --held->refs_;
    if (held->refs_ == 0 && held) delete held;
}

} // namespace

// 0041D1A0: keeps `object` on the timers list for `seconds` (shared into the entry).
void ClientSession::AddTimed(CRefObject* object, std::uint32_t seconds) {
    ClientSession* self = this;
    RefPtr raw;
    RefPtrFromRaw(&raw, object);
    TimedEntry entry;
    entry.key = seconds;
    entry.ref.Share(raw.ptr);
    self->timers_.Insert(self->timers_.End(), entry);
    DestroyInline(&entry.ref);
}
EI_REPLACE_METHOD(0x0041D1A0, ClientSession::AddTimed);

// 0041D260: sends the server a command message, kept 60 seconds on the timers list.
void ClientSession::SendCommand(int kind, const char* text) {
    ClientSession* self = this;
    auto* command = NewMessageMemory<CommandMessage>();
    if (command) {
        ei::SetVtable(command, kMessageVtable);
        command->version_ = 1;
        stlp::StringElement::Default(&command->text_);
        ei::SetVtable(command, CommandMessage::kVtableAddress);
    }
    command->kind_ = static_cast<std::uint32_t>(kind);
    // The inlined assign of a C string: copied over the current characters, the rest cut (the
    // terminator moved down) or appended.
    command->text_.Assign(text);

    RefPtr raw;
    RefPtrFromRaw(&raw, command);
    TimedEntry entry;
    entry.key = kCommandLifetime;
    entry.ref.CopyShared(&raw);
    self->timers_.PushBack(entry);
    entry.ref.Destroy();
    raw.ptr = nullptr;
    raw.Destroy();
}
EI_REPLACE_METHOD(0x0041D260, ClientSession::SendCommand);

// 0041D420 (slot 16): makes `*out` an empty server entry (no name or description, kind 4, no
// players).
void ClientSession::CreateEntryImpl(RefPtr* out) {
    auto* entry = NewMessageMemory<ServerEntry>();
    if (entry) {
        entry->ConstructBase();
        stlp::StringElement::Default(&entry->name_);
        stlp::StringElement::Default(&entry->description_);
        ei::SetVtable(entry, ServerEntry::kVtableAddress);
        const char* empty = game::QuestionMarkText();
        entry->name_.Assign(empty);
        entry->kind_ = 4;
        entry->description_.Assign(empty);   // inlined: overwrite, then cut or append
        entry->players_ = 0;
        entry->maxPlayers_ = 0;
        entry->password_ = 0;
    }
    if (entry == out->ptr) return;
    if (CRefObject* old = out->ptr) {
        if (old->refs_ != 1) {
            old->flags7_ |= CRefObject::kDetached;
            ++old->refs_;
            old->OnShutdown();
            --old->refs_;
        }
        CRefObject* held = out->ptr;
        --held->refs_;
        if (held->refs_ == 0 && held) delete held;
    }
    out->Share(entry);
}
EI_REPLACE_METHOD(0x0041D420, ClientSession::CreateEntryImpl);

// 0041D410 (slot 15): the game flow (007AFE20) begins the multiplayer game.
void ClientSession::OnJoined() { TheGameFlow()->BeginMultiplayerGame(); }
namespace {
void __fastcall ClientBeginGameHook(ClientSession* self) { self->ClientSession::OnJoined(); }
} // namespace
EI_REPLACE(0x0041D410, ClientBeginGameHook);

// 0041D9A0 (called with the client by the transfer screen 0061E4E0): a buffer's stream size (its
// base plus the highest written position, from `begin`) over the dword at its start (1 when 0),
// both as signed integers; 0 for fewer than 4 bytes. Reading the dword moves the read position to
// 4 (Seek(0) through slot 6, then a 4-byte read as BufferRead does it). Returned unrounded: the
// caller multiplies ST0. Plain C++ division (rewrite-guide rule 7).
double ClientSession::BufferRatioImpl(GameBuffer* buffer) {
    auto streamSize = [buffer] {
        if (buffer->cur > buffer->high) buffer->high = buffer->cur;
        return static_cast<std::int32_t>(buffer->base - reinterpret_cast<std::intptr_t>(buffer->begin) +
                                         reinterpret_cast<std::intptr_t>(buffer->high));
    };
    if (streamSize() < 4) {
        float zero;
        std::memcpy(&zero, ei::GamePointer<const void>(0x0073B858), sizeof(zero));
        return zero;
    }
    buffer->Seek(0);
    std::uint32_t stored;
    buffer->ReadBytes(&stored, 4);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return static_cast<double>(streamSize()) / static_cast<double>(divisor);
}
EI_REPLACE_METHOD(0x0041D9A0, ClientSession::BufferRatioImpl);

} // namespace net
