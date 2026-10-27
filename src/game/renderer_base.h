#pragma once

#include <cstddef>

// The renderer's counted base (game.exe's: one slot, the deleting destructor, which no object
// reaches; device_helpers.cpp): CDXSurface, the device object and the transform pipeline derive from it. Holders
// count references; the last one deletes the object (for a surface holder).
class CRendererBase {
public:
    virtual ~CRendererBase();

    // On the game's heap: null without memory, and then no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    void AddRef() { ++refs_; }
    void Release() {
        if (--refs_ == 0) delete this;
    }

    int refs_ = 0;   // +0x04

protected:
    CRendererBase() {}
    CRendererBase(const CRendererBase&) = delete;
    CRendererBase& operator=(const CRendererBase&) = delete;
};
