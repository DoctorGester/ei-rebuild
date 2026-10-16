// The figure loader's constructor (004F68C0, RET 8, returns this; the client world's setup news it
// with the device object and the game's folder). Rebirth patches it (its resource override, at the
// first archive's name), so this is the vanilla body, installed in the vanilla launch only, and
// callers go through its address. A-S12; see docs/notes/renderer.md (section 12).
#include "model.h"
#include "mod_folders.h"
#include "texture.h"   // CTextureDevice

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/stlp.h"

#include <cstddef>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const char* FiguresArchive() { return reinterpret_cast<const char*>(0x0078EC24); }   // "figures.res"
const char* MenusArchive() { return reinterpret_cast<const char*>(0x0078EC18); }     // "menus.res"
constexpr std::size_t kFigureSetBuckets = 100;   // the hint the figure sets' table is created with

} // namespace

// The base class's part (vtable 0073D824), an empty archive list, figure set table and cache, the
// device object counted, the transform object taken from its pipeline, then the two archives of
// `folder` opened (a failure is only logged). The original also leaves bytes of its empty hash,
// equality and compare objects in the containers' padding (stack contents, and the low byte of
// `renderer` in the cache's); nothing reads them and they are not written here.
CLinkFigureLoader* CLinkFigureLoader::Construct(CTextureDevice* renderer, const stlp::string* folder) {
    refCount_ = 0;
    ei::SetVtable(this, 0x0073D824);
    archives_ = static_cast<LinkArchiveNode*>(stlp::allocate(sizeof(LinkArchiveNode)));
    archives_->next = archives_;
    archives_->prev = archives_;
    figureSets_.InitBuckets(kFigureSetBuckets);
    renderer_ = renderer;
    if (renderer) renderer->AddRef();
    cache_.Construct();
    ei::SetVtable(this, CLinkFigureLoader::kVtableAddress);
    pipeline_ = renderer_->pipeline_->transform_;
    // Rebirth's ResOverride (004F6952): the mod's figure archives come first.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& modPath : modfolders::FigureArchives()) {
            const stlp::string path(modPath.c_str());
            AddArchive(&path);
        }
    }
    {
        const stlp::string path = stlp::Plus(*folder, FiguresArchive());
        AddArchive(&path);
    }
    {
        const stlp::string path = stlp::Plus(*folder, MenusArchive());
        AddArchive(&path);
    }
    return this;
}
EI_REPLACE_METHOD(0x004F68C0, CLinkFigureLoader::Construct);
