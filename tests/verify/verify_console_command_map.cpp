// Checks for the console's command maps (src/game/console_command_map.cpp, ticket S16).
//
// - map() 005E0A90: the original builds, its header goes back on top of the pool's free list, then
//   ours: the same header and words (the compare byte is the original's uninitialised less<>
//   temporary and is not compared). Only the static initialisers call it, before the hooks, so a
//   start-up check drives it once on a scratch map.
// - operator[] 005E0B20, ours only: the slot returned is the key's node's, a key already present
//   keeps its node and the count, a new one adds one node, and the keys stay in _mbscmp order.
//   Reached by every console line (the executor registers its commands on each call).
// - ~map 005DC080 and _M_erase 005E0E40, ours only (counted; they run at exit).
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/game/console_command_map.h"

#include <cstring>

namespace {

using console::CommandMap;
using console::CommandMapAbi;
using console::CommandHandler;
using Node = CommandMapAbi::Node;
using stlp_verify::Name;

// The keys of the subtree in strictly increasing order, each node's parent link right.
bool Ordered(const Node* n, const Node* parent, const Node*& previous) {
    if (!n) return true;
    if (n->parent != parent || !Ordered(n->left, n, previous)) return false;
    if (previous && !(previous->first < n->first)) return false;
    previous = n;
    return Ordered(n->right, n, previous);
}

} // namespace

static CommandMap*(__fastcall* s_construct)(CommandMap*);
static CommandMap* __fastcall ConstructCheck(CommandMap* self) {
    s_construct(self);
    unsigned char words[8];
    unsigned char head[sizeof(stlp::RbNodeBase)];
    std::memcpy(words, self, sizeof(words));
    Node* theirs = self->end();
    std::memcpy(head, theirs, sizeof(head));
    stlp::deallocate(theirs, sizeof(Node));
    CommandMap* result = CommandMapAbi::Construct(self);
    // The header's colour byte and links (its other three bytes are the pool's free-list link).
    const unsigned char* ours = reinterpret_cast<const unsigned char*>(self->end());
    bool ok = result == self && self->end() == theirs && std::memcmp(words, self, sizeof(words)) == 0 &&
              head[0] == ours[0] && std::memcmp(head + 4, ours + 4, sizeof(head) - 4) == 0;
    ei::verify::Report(Name<0x005E0A90>("stlp::map::map"), ok, "");
    return result;
}
// PUSH -1; PUSH imm32 (7).
EI_VERIFY(0x005E0A90, ConstructCheck, 7, s_construct);

static CommandHandler* __fastcall SubscriptCheck(CommandMap* self, void*, const mfc::CString* key) {
    std::size_t before = self->size();
    Node* existing = self->find(*key);
    CommandHandler* result = CommandMapAbi::Subscript(self, nullptr, key);
    Node* node = self->find(*key);
    const Node* previous = nullptr;
    bool ok = node != self->end() && result == &node->second &&
              (existing != self->end() ? existing == node && self->size() == before
                                       : self->size() == before + 1 && !node->second) &&
              Ordered(self->end()->parent, self->end(), previous);
    ei::verify::Report(Name<0x005E0B20>("stlp::map::operator[]"), ok, static_cast<const char*>(*key));
    return result;
}
static void* s_unused;
EI_VERIFY(0x005E0B20, SubscriptCheck, 0, s_unused);

static void __fastcall DestroyCheck(CommandMap* self) {
    CommandMapAbi::Destroy(self);
    ei::verify::Report(Name<0x005DC080>("stlp::map::~map"), true, "");
}
EI_VERIFY(0x005DC080, DestroyCheck, 0, s_unused);

static void __fastcall EraseSubtreeCheck(CommandMap* self, void*, Node* x) {
    CommandMapAbi::EraseSubtree(self, nullptr, x);
    ei::verify::Report(Name<0x005E0E40>("stlp::_Rb_tree::_M_erase"), true, "");
}
EI_VERIFY(0x005E0E40, EraseSubtreeCheck, 0, s_unused);

namespace {
// map() once on a scratch map (the original and ours through ConstructCheck), then a few
// operator[] calls and ours destroys it.
void CommandMapStartupCheck() {
    alignas(CommandMap) static unsigned char storage[sizeof(CommandMap)];
    auto* map = reinterpret_cast<CommandMap*>(storage);
    ConstructCheck(map);
    for (const char* name : {"help", "quit", "Help", "a", "help", "zz"}) {
        mfc::CString key(name);
        SubscriptCheck(map, nullptr, &key);
    }
    DestroyCheck(map);
}
} // namespace
EI_STARTUP_CHECK(CommandMapStartupCheck);
