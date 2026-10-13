// Check for link_loader_create.cpp (A-S12): the figure loader's constructor 004F68C0 (the client
// world's setup: every game start). Vanilla launch only (Rebirth patches the function, so nothing
// is installed there). Side by side, one after the other: the original builds a loader in a scratch
// block, which is summed up and destroyed again (its archives closed, its device reference given
// back) before ours builds the real one. Compared: the result, the vtable, the count, the
// transform object, the device and its reference count, the archives' names in order and whether
// each has its resource, the figure sets' table (bucket count, empty) and the cache (empty). The
// containers' padding bytes are not compared (the original leaves stack contents there). Main
// thread.
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/lib/stlp.h"

#include <cstddef>
#include <string>
#include <vector>

CLinkFigureLoader* __fastcall LinkFigureLoaderConstruct(CLinkFigureLoader* self, void*, CTextureDevice* renderer,
                                                        const stlp::string* folder);

namespace {

struct Summary {
    void* vtable;
    int refs;
    ITransformPipeline* pipeline;
    CTextureDevice* renderer;
    int rendererRefs;
    std::vector<std::string> archives;
    std::vector<bool> resources;
    std::size_t buckets;
    std::size_t usedBuckets;
    std::size_t sets;
    bool cacheEmpty;

    bool operator==(const Summary&) const = default;
};

Summary Summarise(CLinkFigureLoader* loader) {
    Summary s{};
    s.vtable = *reinterpret_cast<void**>(loader);
    s.refs = loader->refCount_;
    s.pipeline = loader->pipeline_;
    s.renderer = loader->renderer_;
    s.rendererRefs = loader->renderer_ ? loader->renderer_->refs_ : 0;
    for (LinkArchiveNode* node = loader->archives_->next; node != loader->archives_; node = node->next) {
        s.archives.emplace_back(node->name.data(), node->name.size());
        s.resources.push_back(node->resource != nullptr);
    }
    s.buckets = loader->figureSets_.BucketCount();
    for (std::size_t b = 0; b < s.buckets; ++b) s.usedBuckets += loader->figureSets_.Bucket(b) != nullptr;
    s.sets = loader->figureSets_.Size();
    s.cacheEmpty = loader->cache_.begin() == loader->cache_.end();
    return s;
}

// 004F68C0: PUSH -1 (2), PUSH 0x0071C2AC (5).
CLinkFigureLoader*(__fastcall* s_construct)(CLinkFigureLoader*, void*, CTextureDevice*, const stlp::string*);
CLinkFigureLoader* __fastcall ConstructVerify(CLinkFigureLoader* self, void*, CTextureDevice* renderer,
                                              const stlp::string* folder) {
    alignas(4) unsigned char scratch[sizeof(CLinkFigureLoader)];
    auto* theirs = reinterpret_cast<CLinkFigureLoader*>(scratch);
    const bool returnedThis = s_construct(theirs, nullptr, renderer, folder) == theirs;
    const Summary expected = Summarise(theirs);
    theirs->Destruct();

    CLinkFigureLoader* result = LinkFigureLoaderConstruct(self, nullptr, renderer, folder);
    const Summary actual = Summarise(self);
    ei::verify::Check("CLinkFigureLoader constructor (004F68C0)", true, returnedThis && result == self && expected == actual,
                      "\"%s\": %u vs %u archives, %u vs %u buckets, device references %d vs %d", folder->data(),
                      static_cast<unsigned>(expected.archives.size()), static_cast<unsigned>(actual.archives.size()),
                      static_cast<unsigned>(expected.buckets), static_cast<unsigned>(actual.buckets),
                      expected.rendererRefs, actual.rendererRefs);
    return result;
}

} // namespace

EI_VERIFY(0x004F68C0, ConstructVerify, 7, s_construct);
