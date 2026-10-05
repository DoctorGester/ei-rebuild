#pragma once

// A CStream a test can derive from: the slots the save code never calls do nothing. The derived
// class overrides Read and Write; the object is a plain C++ object with its own vtable (the game's
// CStream destructor body is never run on it).

#include "../../src/game/core_streams.h"

class FakeStream : public CStream {
public:
    FakeStream() {
        reading_ = 0;
        ok_ = 1;
        unknown_6[0] = unknown_6[1] = 0;
        version_ = 2000000000;
    }
    void Seek(std::uint32_t) override {}
    std::uint32_t Size() override { return 0; }
    std::uint32_t Position() override { return 0; }
    void Clear() override {}
    void CopyFrom(CStream*) override {}
};
