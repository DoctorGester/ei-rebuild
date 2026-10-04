#pragma once

// Save and restore of a DeltaState (src/game/net_delta.h) for verify wrappers of whole-object
// serialisers: copy the state, run the original, copy its result, put the saved state back, run
// ours, compare. Every stream (the two headers and each field's two snapshots and two outputs) is
// deep-copied; only memory streams (the two CMemoryStream vtables) qualify, since another stream
// class would share its resource with the copy. The read-side globals are saved with the state.

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

} // namespace deltasnap
