#pragma once

// Copies of the network delta coder's streams and states (src/game/net_delta.h) for verify wrappers.
//  - Kept and twin (the order of the checks that may allocate, SV2f / SV2h / SV2h3): a stream's
//    fields and bytes are kept in our memory (KeptStream, KeptField, KeptState) before ours runs on
//    the real objects; then, inside a heap::Aside (harness_heap.h), the original runs on a twin made
//    from them (TwinStream, TwinField, TwinState: game-heap buffers, as the original may grow them)
//    and the twin is freed. Ours' blocks are then the only ones the check makes in the reservation,
//    where a --no-hooks run makes the original's; the twin's blocks and whatever the original makes
//    for itself come from the heap outside it.
//    Where the original must run on the real object (verify_net_state.cpp's readers), the twin is
//    put into the state in place (KeptState::CloneInto) with ours' state set aside (StateAside),
//    and freed (FreeStateBuffers) before ours' is put back.
//  - StateCopy: save and restore of a whole DeltaState for the checks that run the original on the
//    real state first (verify_net_state.cpp's writers): copy the state, run the original, copy its
//    result, put the saved state back, run ours, compare.
// Only memory streams (the two CMemoryStream vtables) qualify, since another stream class would
// share its resource with the copy. The read-side globals are saved with the state.

#include "../../src/game/net_delta.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace deltasnap {

inline bool IsMemoryStream(const BitStream* s) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, s, sizeof(vtable));
    return vtable == 0x0073BD90 || vtable == 0x0073C960;
}

inline void CloneStream(BitStream* dst, const BitStream* src) {
    std::memcpy(dst, src, sizeof(BitStream));
    if (!src->data_) return;
    std::uint32_t bytes = src->capacity_ > src->size_ ? src->capacity_ : src->size_;
    if (bytes == 0) bytes = 1;
    dst->data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
    std::memset(dst->data_, 0, bytes);
    std::memcpy(dst->data_, src->data_, src->size_);
}

inline void FreeStream(BitStream* s) {
    if (s->data_) gamecrt::operator_delete(s->data_);
    s->data_ = nullptr;
}

// Same fields (the buffer pointer excepted) and the same bytes up to the size.
inline bool SameStream(const BitStream* a, const BitStream* b) {
    constexpr std::size_t kData = offsetof(BitStream, data_);
    const auto* x = reinterpret_cast<const unsigned char*>(a);
    const auto* y = reinterpret_cast<const unsigned char*>(b);
    if (std::memcmp(x, y, kData) != 0) return false;
    if (std::memcmp(x + kData + 4, y + kData + 4, sizeof(BitStream) - kData - 4) != 0) return false;
    if (a->size_ == 0) return true;
    if (!a->data_ || !b->data_) return a->data_ == b->data_;
    return std::memcmp(a->data_, b->data_, a->size_) == 0;
}

struct Globals {
    int keep0, keep1;
    BitStream* snapshot;
    BitStream* reader;
    static Globals Save() {
        return {delta::KeepSnapshot0(), delta::KeepSnapshot1(), delta::ReadSnapshot(), delta::Reader()};
    }
    void Restore() const {
        delta::KeepSnapshot0() = keep0;
        delta::KeepSnapshot1() = keep1;
        delta::ReadSnapshot() = snapshot;
        delta::Reader() = reader;
    }
    bool operator==(const Globals& o) const {
        return keep0 == o.keep0 && keep1 == o.keep1 && snapshot == o.snapshot && reader == o.reader;
    }
};

// A deep copy of a DeltaState. `valid` is false (and nothing is copied) when a stream is not a
// memory stream or the field count is out of range.
class StateCopy {
public:
    explicit StateCopy(const DeltaState* s) {
        valid_ = s->fieldCount_ >= 0 && s->fieldCount_ <= 64 && IsMemoryStream(&s->header_[0]) &&
                 IsMemoryStream(&s->header_[1]);
        for (int i = 0; valid_ && i < s->fieldCount_; ++i)
            for (int c = 0; c < 2; ++c)
                valid_ = valid_ && IsMemoryStream(&s->fields_[i].snapshot[c]) && IsMemoryStream(&s->fields_[i].out[c]);
        if (!valid_) return;
        std::memcpy(head_, s, sizeof(head_));
        for (int c = 0; c < 2; ++c) CloneStream(&header_[c], &s->header_[c]);
        fields_.resize(s->fieldCount_);
        for (int i = 0; i < s->fieldCount_; ++i)
            for (int c = 0; c < 2; ++c) {
                CloneStream(&fields_[i].snapshot[c], &s->fields_[i].snapshot[c]);
                CloneStream(&fields_[i].out[c], &s->fields_[i].out[c]);
            }
        owns_ = true;
    }
    StateCopy(const StateCopy&) = delete;
    StateCopy& operator=(const StateCopy&) = delete;
    ~StateCopy() { Release(); }

    bool valid() const { return valid_; }

    // Puts the copy back into `s` (whose buffers are freed); the copy gives up its buffers.
    void MoveInto(DeltaState* s) {
        DeltaField* fields = s->fields_;
        std::memcpy(s, head_, sizeof(head_));
        s->fields_ = fields;
        for (int c = 0; c < 2; ++c) {
            FreeStream(&s->header_[c]);
            std::memcpy(&s->header_[c], &header_[c], sizeof(BitStream));
        }
        for (std::size_t i = 0; i < fields_.size(); ++i)
            for (int c = 0; c < 2; ++c) {
                FreeStream(&fields[i].snapshot[c]);
                FreeStream(&fields[i].out[c]);
                std::memcpy(&fields[i].snapshot[c], &fields_[i].snapshot[c], sizeof(BitStream));
                std::memcpy(&fields[i].out[c], &fields_[i].out[c], sizeof(BitStream));
            }
        owns_ = false;
    }

    // Whether `s` holds the same state (the field array's address excepted).
    bool Same(const DeltaState* s) const {
        if (std::memcmp(head_, s, offsetof(DeltaState, fields_)) != 0) return false;
        constexpr std::size_t kAfter = offsetof(DeltaState, fieldCount_);
        if (std::memcmp(head_ + kAfter, reinterpret_cast<const unsigned char*>(s) + kAfter, sizeof(head_) - kAfter) != 0)
            return false;
        if (static_cast<int>(fields_.size()) != s->fieldCount_) return false;
        for (int c = 0; c < 2; ++c)
            if (!SameStream(&header_[c], &s->header_[c])) return false;
        for (std::size_t i = 0; i < fields_.size(); ++i)
            for (int c = 0; c < 2; ++c)
                if (!SameStream(&fields_[i].snapshot[c], &s->fields_[i].snapshot[c]) ||
                    !SameStream(&fields_[i].out[c], &s->fields_[i].out[c]))
                    return false;
        return true;
    }

private:
    void Release() {
        if (!owns_) return;
        for (int c = 0; c < 2; ++c) FreeStream(&header_[c]);
        for (auto& f : fields_)
            for (int c = 0; c < 2; ++c) {
                FreeStream(&f.snapshot[c]);
                FreeStream(&f.out[c]);
            }
        owns_ = false;
    }

    bool valid_ = false;
    bool owns_ = false;
    unsigned char head_[offsetof(DeltaState, header_)] = {};
    BitStream header_[2] = {};
    std::vector<DeltaField> fields_;
};

// --- Kept bytes and twins ----------------------------------------------------------------------

// A stream's fields and bytes (up to its size) kept in our memory.
class KeptStream {
public:
    KeptStream() = default;
    explicit KeptStream(const BitStream* s) { Keep(s); }
    void Keep(const BitStream* s) {
        std::memcpy(header_, s, sizeof(header_));
        bytes_.clear();
        block_ = 0;
        if (!s->data_) return;
        block_ = s->capacity_ > s->size_ ? s->capacity_ : s->size_;
        if (block_ == 0) block_ = 1;
        bytes_.assign(s->data_, s->data_ + s->size_);
    }
    // The deep copy CloneStream would have made of the stream when it was kept (its buffer a block
    // of the game heap, which the original may grow: free and make anew).
    void CloneTo(BitStream* dst) const {
        std::memcpy(dst, header_, sizeof(header_));
        if (!block_) return;
        dst->data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(block_));
        std::memset(dst->data_, 0, block_);
        if (!bytes_.empty()) std::memcpy(dst->data_, bytes_.data(), bytes_.size());
    }

private:
    alignas(8) unsigned char header_[sizeof(BitStream)] = {};
    std::vector<std::uint8_t> bytes_;
    std::uint32_t block_ = 0;   // the buffer's size; 0: no buffer
};

// A twin of a kept stream, made when the original is about to run, freed with the scope.
class TwinStream {
public:
    TwinStream() = default;
    TwinStream(const TwinStream&) = delete;
    TwinStream& operator=(const TwinStream&) = delete;
    ~TwinStream() {
        if (made_) FreeStream(&stream);
    }
    BitStream* Make(const KeptStream& kept) {
        kept.CloneTo(&stream);
        made_ = true;
        return &stream;
    }
    BitStream stream;

private:
    bool made_ = false;
};

// A field's four streams kept.
struct KeptField {
    KeptStream snapshot[2];
    KeptStream out[2];
    explicit KeptField(const DeltaField* f) {
        for (int c = 0; c < 2; ++c) {
            snapshot[c].Keep(&f->snapshot[c]);
            out[c].Keep(&f->out[c]);
        }
    }
    void CloneTo(DeltaField* dst) const {
        for (int c = 0; c < 2; ++c) {
            snapshot[c].CloneTo(&dst->snapshot[c]);
            out[c].CloneTo(&dst->out[c]);
        }
    }
};

// Frees a field copy's four buffers, in the reverse of KeptField::CloneTo's order.
inline void FreeField(DeltaField* f) {
    for (int c = 1; c >= 0; --c) {
        FreeStream(&f->out[c]);
        FreeStream(&f->snapshot[c]);
    }
}

// A twin of a kept field (freed with the scope).
class TwinField {
public:
    TwinField() = default;
    TwinField(const TwinField&) = delete;
    TwinField& operator=(const TwinField&) = delete;
    ~TwinField() {
        if (made_) FreeField(&field);
    }
    DeltaField* Make(const KeptField& kept) {
        kept.CloneTo(&field);
        made_ = true;
        return &field;
    }
    // The same fields (buffer pointers excepted) and bytes as `other`.
    bool Same(const DeltaField* other) const {
        for (int c = 0; c < 2; ++c)
            if (!SameStream(&field.snapshot[c], &other->snapshot[c]) || !SameStream(&field.out[c], &other->out[c]))
                return false;
        return true;
    }
    DeltaField field;

private:
    bool made_ = false;
};

// Whether the field count is in range and every stream of the state is a memory stream.
inline bool CopyableState(const DeltaState* s) {
    bool ok = s->fieldCount_ >= 0 && s->fieldCount_ <= 64 && IsMemoryStream(&s->header_[0]) &&
              IsMemoryStream(&s->header_[1]);
    for (int i = 0; ok && i < s->fieldCount_; ++i)
        for (int c = 0; c < 2; ++c)
            ok = ok && IsMemoryStream(&s->fields_[i].snapshot[c]) && IsMemoryStream(&s->fields_[i].out[c]);
    return ok;
}

// A whole DeltaState kept: the bytes before the headers, the two headers and every field.
class KeptState {
public:
    explicit KeptState(const DeltaState* s) {
        valid_ = CopyableState(s);
        if (!valid_) return;
        std::memcpy(head_, s, sizeof(head_));
        for (int c = 0; c < 2; ++c) header_[c].Keep(&s->header_[c]);
        fields_.reserve(static_cast<std::size_t>(s->fieldCount_));
        for (int i = 0; i < s->fieldCount_; ++i) fields_.emplace_back(&s->fields_[i]);
    }
    bool valid() const { return valid_; }
    int fieldCount() const { return static_cast<int>(fields_.size()); }

    // A twin put into `s` in place, over what it holds (set aside first: StateAside): the kept
    // bytes (its field array kept), every stream's buffer a new game-heap block (FreeStateBuffers).
    void CloneInto(DeltaState* s) const {
        DeltaField* fields = s->fields_;
        std::memcpy(s, head_, sizeof(head_));
        s->fields_ = fields;
        for (int c = 0; c < 2; ++c) header_[c].CloneTo(&s->header_[c]);
        for (std::size_t i = 0; i < fields_.size(); ++i) fields_[i].CloneTo(&fields[i]);
    }

private:
    friend class TwinState;
    bool valid_ = false;
    unsigned char head_[offsetof(DeltaState, header_)] = {};
    KeptStream header_[2];
    std::vector<KeptField> fields_;
};

// A twin of a kept state for the original: the state and its field array in our memory, every
// stream's buffer a game-heap block (freed with the scope, in the reverse of the order made).
class TwinState {
public:
    explicit TwinState(const KeptState& kept) : count_(kept.fields_.size()), fields_(count_ + 1) {   // never empty
        DeltaState* s = get();
        std::memcpy(raw_, kept.head_, sizeof(kept.head_));
        for (int c = 0; c < 2; ++c) kept.header_[c].CloneTo(&s->header_[c]);
        for (std::size_t i = 0; i < count_; ++i) kept.fields_[i].CloneTo(&fields_[i]);
        s->fields_ = fields_.data();
    }
    TwinState(const TwinState&) = delete;
    TwinState& operator=(const TwinState&) = delete;
    ~TwinState() {
        for (std::size_t i = count_; i-- > 0;) FreeField(&fields_[i]);
        for (int c = 1; c >= 0; --c) FreeStream(&get()->header_[c]);
    }
    DeltaState* get() { return reinterpret_cast<DeltaState*>(raw_); }
    const DeltaState* get() const { return reinterpret_cast<const DeltaState*>(raw_); }

    // Whether `s` holds the same state (the field array's address and the buffer pointers excepted).
    bool Same(const DeltaState* s) const {
        const DeltaState* t = get();
        constexpr std::size_t kAfter = offsetof(DeltaState, fieldCount_);
        if (std::memcmp(t, s, offsetof(DeltaState, fields_)) != 0) return false;
        if (std::memcmp(raw_ + kAfter, reinterpret_cast<const unsigned char*>(s) + kAfter,
                        offsetof(DeltaState, header_) - kAfter) != 0)
            return false;
        if (static_cast<int>(count_) != s->fieldCount_) return false;
        for (int c = 0; c < 2; ++c)
            if (!SameStream(&t->header_[c], &s->header_[c])) return false;
        for (std::size_t i = 0; i < count_; ++i)
            for (int c = 0; c < 2; ++c)
                if (!SameStream(&fields_[i].snapshot[c], &s->fields_[i].snapshot[c]) ||
                    !SameStream(&fields_[i].out[c], &s->fields_[i].out[c]))
                    return false;
        return true;
    }

private:
    alignas(8) unsigned char raw_[sizeof(DeltaState)] = {};
    std::size_t count_;
    std::vector<DeltaField> fields_;
};

// Frees a state's stream buffers, in the reverse of KeptState::CloneInto's order.
inline void FreeStateBuffers(DeltaState* s) {
    for (int i = s->fieldCount_; i-- > 0;) FreeField(&s->fields_[i]);
    for (int c = 1; c >= 0; --c) FreeStream(&s->header_[c]);
}

// A state as it is, set aside in our memory (its bytes and its fields' streams; the buffers stay
// the state's) while a twin takes its place, and put back.
class StateAside {
public:
    explicit StateAside(const DeltaState* s)
        : count_(static_cast<std::size_t>(s->fieldCount_)), fields_(count_ * sizeof(DeltaField) + 1) {
        std::memcpy(raw_, s, sizeof(raw_));
        if (count_) std::memcpy(fields_.data(), s->fields_, count_ * sizeof(DeltaField));
    }
    void PutBack(DeltaState* s) const {
        std::memcpy(s, raw_, sizeof(raw_));
        if (count_) std::memcpy(s->fields_, fields_.data(), count_ * sizeof(DeltaField));
    }
    // Whether `s` holds the same state (the buffer pointers excepted).
    bool Same(const DeltaState* s) const {
        if (std::memcmp(raw_, s, offsetof(DeltaState, header_)) != 0) return false;
        const auto* t = reinterpret_cast<const DeltaState*>(raw_);
        for (int c = 0; c < 2; ++c)
            if (!SameStream(&t->header_[c], &s->header_[c])) return false;
        const auto* f = reinterpret_cast<const DeltaField*>(fields_.data());
        for (std::size_t i = 0; i < count_; ++i)
            for (int c = 0; c < 2; ++c)
                if (!SameStream(&f[i].snapshot[c], &s->fields_[i].snapshot[c]) ||
                    !SameStream(&f[i].out[c], &s->fields_[i].out[c]))
                    return false;
        return true;
    }

private:
    alignas(8) unsigned char raw_[sizeof(DeltaState)] = {};
    std::size_t count_;
    std::vector<unsigned char> fields_;
};

} // namespace deltasnap
