#pragma once

// Helpers of the list<T> checks (verify_stlp_list.cpp, verify_stlp_list_refs.cpp).

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace stlp_verify {

// The node allocator's free list for blocks of `bytes` (16 lists of 8-byte steps from 0079B224);
// null above 128 bytes, which come from operator new.
inline void** FreeList(std::size_t bytes) {
    if (bytes > 128) return nullptr;
    return reinterpret_cast<void**>(0x0079B224 + 4 * ((bytes - 1) >> 3));
}

// While one is alive, blocks of our memory lie on top of every node allocator free list, so that an
// original run on a scratch object (after ours ran on the real one) or a start-up check takes and
// gives back only those: the game's pooled blocks are neither taken nor reordered and no list is
// refilled from the heap (S12g). At most kBlocks per list may be held at once; blocks above 128
// bytes still come from operator new. Main thread only (nothing else may use the lists meanwhile).
// Restore() takes ours off again and says whether all of them were back on top.
class OwnBlocks {
public:
    static constexpr int kLists = 16;
    static constexpr int kBlocks = 8;

    OwnBlocks() {
        for (int c = 0; c < kLists; ++c) {
            void** list = FreeList(static_cast<std::size_t>(c + 1) * 8);
            heads_[c] = *list;
            for (int i = 0; i < kBlocks; ++i) {
                void** block = reinterpret_cast<void**>(s_blocks[c][i]);
                *block = *list;
                *list = block;
            }
        }
    }
    OwnBlocks(const OwnBlocks&) = delete;
    OwnBlocks& operator=(const OwnBlocks&) = delete;
    ~OwnBlocks() {
        if (!restored_) Restore();
    }
    bool Restore() {
        restored_ = true;
        bool ok = true;
        for (int c = 0; c < kLists; ++c) {
            void** list = FreeList(static_cast<std::size_t>(c + 1) * 8);
            void* p = *list;
            for (int i = 0; i < kBlocks && ok; ++i) {
                ok = Ours(c, p);
                if (ok) p = *static_cast<void**>(p);
            }
            ok = ok && p == heads_[c];
            *list = heads_[c];
        }
        return ok;
    }

private:
    static bool Ours(int c, const void* p) {
        const auto* first = reinterpret_cast<const unsigned char*>(s_blocks[c]);
        const auto* q = static_cast<const unsigned char*>(p);
        return q >= first && q < first + sizeof(s_blocks[c]) && (q - first) % sizeof(s_blocks[c][0]) == 0;
    }
    alignas(8) static inline unsigned char s_blocks[kLists][kBlocks][128];
    void* heads_[kLists];
    bool restored_ = false;
};

// One report name per instantiation ("stlp::list::end 004049D0"), so the counts show which are
// reached. Each address is checked under one kind only.
template <std::uintptr_t A>
const char* Name(const char* kind) {
    static char name[64];
    static const bool once = (std::snprintf(name, sizeof(name), "%s %08X", kind, static_cast<unsigned>(A)), true);
    (void)once;
    return name;
}

// `node` linked in between `prev` and `next`, both ways.
template <typename N>
bool LinkedBetween(const N* node, const N* prev, const N* next) {
    return node->prev == prev && node->next == next && prev->next == node && next->prev == node;
}

} // namespace stlp_verify
