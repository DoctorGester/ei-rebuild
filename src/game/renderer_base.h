#pragma once

#include <cstddef>

// The renderer's counted base (one virtual, the deleting destructor, which no object
// reaches; renderer_scene.cpp): the surface holder (Surface), the device object and the transform pipeline derive from it. Holders
// count references; the last one deletes the object (for a surface holder).
class RendererBase {
public:
    virtual ~RendererBase();


    void AddRef() { ++refs; }
    void Release() {
        if (--refs == 0) delete this;
    }

    int refs = 0;

protected:
    RendererBase() {}
    RendererBase(const RendererBase&) = delete;
    RendererBase& operator=(const RendererBase&) = delete;
};
