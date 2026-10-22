// The figure loader's constructor (004F68C0, RET 8, returns this; the client world's setup news it
// with the device object and the game's folder). Rebirth patches it (its resource override, at the
// first archive's name), so this is the vanilla body, installed in the vanilla launch only, and
// callers go through its address. A-S12; see docs/notes/renderer.md (section 12).
#include "core_refcount.h"
#include "model.h"
#include "mod_folders.h"
#include "texture.h"   // CTextureDevice

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstddef>

namespace {

const char* FiguresArchive() { return "figures.res"; }
const char* MenusArchive() { return "menus.res"; }
constexpr std::size_t kFigureSetBuckets = 100;   // the hint the figure sets' table is created with

extern "C" int __cdecl _purecall();

// The compiled tables (C19, lib/compiled_vtable.h) in place of game.exe's 0073D808 (the slots' bodies, link_loader.cpp)
// and its base's 0073D824.
struct LinkFigureLoaderClass final : ei::CompiledTag<LinkFigureLoaderClass> {
    CLinkFigureLoader* Loader() { return ObjectAs<CLinkFigureLoader>(); }
    virtual void* DeleteThis(unsigned flags) { return Loader()->DeleteThisImpl(flags); }   // 0
    virtual int CacheSize() { return Loader()->CLinkFigureLoader::CacheSize(); }           // 1: 004F6A20
    virtual int CacheBytes() { return Loader()->CLinkFigureLoader::CacheBytes(); }         // 2: 004FAAA0
    virtual CLinkMesh* LoadFigure(const stlp::string* name, const stlp::vector<stlp::string>* parts, float s, float t,
                                  float u, bool fresh) {                                    // 3: 004F7150
        return Loader()->LoadFigureImpl(name, parts, s, t, u, fresh);
    }
    virtual CLinkMesh* LoadLink(const stlp::string* name, float s, float t, float u, bool fresh) {   // 4: 004F7080
        return Loader()->LoadLinkImpl(name, s, t, u, fresh);
    }
    virtual void ClearCache() { Loader()->CLinkFigureLoader::ClearCache(); }               // 5: 004F7020
    virtual CLinkMesh* Remorph(CLinkMesh* mesh, float s, float t, float u) {                // 6: 004F75E0
        return Loader()->RemorphImpl(mesh, s, t, u);
    }
};
struct LinkFigureLoaderBaseClass final : ei::CompiledTag<LinkFigureLoaderBaseClass> {
    virtual void* DeleteThis(unsigned flags) {   // 0: 004F6A00
        ei::SetVtable(this, PlainRootVtable());
        if (flags & 1) gamecrt::operator_delete(this);
        return this;
    }
    virtual void Pure1() { _purecall(); }   // 1-6: pure
    virtual void Pure2() { _purecall(); }
    virtual void Pure3() { _purecall(); }
    virtual void Pure4() { _purecall(); }
    virtual void Pure5() { _purecall(); }
    virtual void Pure6() { _purecall(); }
};

} // namespace

void* CLinkFigureLoader::Vtable() { return LinkFigureLoaderClass::Vtable(); }
void* CLinkFigureLoader::BaseVtable() { return LinkFigureLoaderBaseClass::Vtable(); }

// The base class's part (BaseVtable()), an empty archive list, figure set table and cache, the
// device object counted, the transform object taken from its pipeline, then the two archives of
// `folder` opened (a failure is only logged). The original also leaves bytes of its empty hash,
// equality and compare objects in the containers' padding (stack contents, and the low byte of
// `renderer` in the cache's); nothing reads them and they are not written here.
CLinkFigureLoader* CLinkFigureLoader::Construct(CTextureDevice* renderer, const stlp::string* folder) {
    refCount_ = 0;
    ei::SetVtable(this, BaseVtable());
    archives_ = stlp::allocate_as<LinkArchiveNode>();
    archives_->next = archives_;
    archives_->prev = archives_;
    figureSets_.InitBuckets(kFigureSetBuckets);
    renderer_ = renderer;
    if (renderer) renderer->AddRef();
    cache_.Construct();
    ei::SetVtable(this, Vtable());
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
