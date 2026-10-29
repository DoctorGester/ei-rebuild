#pragma once

#include <cstddef>

// The renderer's counted base (one virtual, the deleting destructor, which no object
// reaches; device_helpers.cpp): the surface holder (Surface), the device object and the transform pipeline derive from it. Holders
// count references; the last one deletes the object (for a surface holder).
class CRendererBase {
public:
    virtual ~CRendererBase();


    void AddRef() { ++refs; }
    void Release() {
        if (--refs == 0) delete this;
    }

    int refs = 0;

protected:
    CRendererBase() {}
    CRendererBase(const CRendererBase&) = delete;
    CRendererBase& operator=(const CRendererBase&) = delete;
};
