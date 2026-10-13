// The figure loader's constructor (004F68C0, RET 8, returns this; the client world's setup news it
// with the device object and the game's folder). Rebirth patches it (its resource override, at the
// first archive's name), so this is the vanilla body, installed in the vanilla launch only, and
// callers go through its address. A-S12; see docs/notes/renderer.md (section 12).
#include "model.h"
#include "texture.h"   // CTextureDevice

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/stlp.h"

#include <cstddef>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// 004F6D30 through its address: the verify build's wrapper runs there.
__declspec(naked) int __fastcall AddArchiveOriginal(CLinkFigureLoader*, void*, const stlp::string* /*path*/)
    EI_ORIGINAL(0x004F6D30)

const char* FiguresArchive() { return reinterpret_cast<const char*>(0x0078EC24); }   // "figures.res"
const char* MenusArchive() { return reinterpret_cast<const char*>(0x0078EC18); }     // "menus.res"
constexpr std::size_t kFigureSetBuckets = 100;   // the hint the figure sets' table is created with

} // namespace

// The base class's part (vtable 0073D824), an empty archive list, figure set table and cache, the
// device object counted, the transform object taken from its pipeline, then the two archives of
// `folder` opened (a failure is only logged). The original also leaves bytes of its empty hash,
// equality and compare objects in the containers' padding (stack contents, and the low byte of
// `renderer` in the cache's); nothing reads them and they are not written here.
CLinkFigureLoader* __fastcall LinkFigureLoaderConstruct(CLinkFigureLoader* self, void*, CTextureDevice* renderer,
                                                        const stlp::string* folder) {
    self->refCount_ = 0;
    ei::SetVtable(self, 0x0073D824);
    self->archives_ = static_cast<LinkArchiveNode*>(stlp::allocate(sizeof(LinkArchiveNode)));
    self->archives_->next = self->archives_;
    self->archives_->prev = self->archives_;
    self->figureSets_.InitBuckets(kFigureSetBuckets);
    self->renderer_ = renderer;
    if (renderer) renderer->AddRef();
    self->cache_.Construct();
    ei::SetVtable(self, CLinkFigureLoader::kVtableAddress);
    self->pipeline_ = self->renderer_->pipeline_->transform_;
    {
        const stlp::string path = stlp::Plus(*folder, FiguresArchive());
        AddArchiveOriginal(self, nullptr, &path);
    }
    {
        const stlp::string path = stlp::Plus(*folder, MenusArchive());
        AddArchiveOriginal(self, nullptr, &path);
    }
    return self;
}
EI_REPLACE(0x004F68C0, LinkFigureLoaderConstruct);
