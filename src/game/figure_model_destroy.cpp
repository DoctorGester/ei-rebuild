// The figure model's destructor (FigureModel, model.h). Every cached animation is deleted, the map cleared
// (each node's pair destroyed, the node returned to the STLport node allocator), each archive closed
// (CloseResource) and deleted, then the map's own destructor runs inline: a second clear (empty by then) and
// the buckets freed. See docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel

#include "record_lifetime.h"

#include "lib/crt.h"
#include "lib/stlp_hash.h"
#include "lib/stlp.h"
#include "lib/mfc_slots.h"

#include <cstdint>

namespace {

// The bucket vector of one of the two maps (STLport's hashtable keeps it after the functors).
template <typename Map>
stlp::vector<typename Map::Node*>& Buckets(Map& table) {
    return *static_cast<stlp::vector<typename Map::Node*>*>(table.BucketVector());
}

// STLport deallocate(p, bytes), and the node allocator's deallocate(p, n).
void __fastcall Deallocate(void* a0, std::size_t a1) { libcrt::NodeDeallocate(a0, a1); }
// Deallocates `count` pointers (the buckets).
void __fastcall DeallocatePointers(void* a0, int a1) { stlp::deallocate(a0, static_cast<std::size_t>(a1) * sizeof(void*)); }

// The figure manager's helpers: the model destructor (through its original address), the pair destructor, the
// node allocator's deallocate, the base destructor.

// Visits every node as the game's iterator walks them: the chain, then the head of the next
// non-empty bucket after the one the node's key hashes to.
template <typename Map, typename F>
void ForEachNode(Map& table, F visit) {
    typename Map::Node* node = table.First();
    while (node) {
        visit(node);
        if (node->next) {
            node = node->next;
            continue;
        }
        std::uint32_t b = stlp::HashString(&node->key) % table.BucketCount();
        node = nullptr;
        while (++b < table.BucketCount())
            if ((node = table.Bucket(b)) != nullptr) break;
    }
}

} // namespace

void __fastcall DestroyFigureModel(FigureModel* model) {
    using Node = FigureModel::AnimationMap::Node;
    FigureModel::AnimationMap& table = model->animationCache;
    // Every value, in iteration order.
    ForEachNode(table, [](Node* node) {
        if (node->value) delete node->value;
    });
    // clear().
    table.ClearWith([](Node* n) {
        std::destroy_at(&n->key);
        if (n) Deallocate(n, sizeof(Node));
    });

    static constexpr CMappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (CMappedResource* FigureModel::*archive : kArchives) (model->*archive)->CloseResource();
    for (CMappedResource* FigureModel::*archive : kArchives)
        if (model->*archive) delete (model->*archive);

    // The hash_map's destructor.
    table.ClearWith([&table](Node* n) {
        std::destroy_at(&n->key);
        stlp::deallocate(n, sizeof(Node));
    });
    stlp::vector<Node*>& buckets = Buckets(table);
    DeallocatePointers(buckets.Start(), static_cast<int>(buckets.EndOfStorage() - buckets.Start()));
}

// The figure manager's destructor: every cached model destroyed and freed, the base's clean-up, the model map
// cleared and its buckets freed (the map's inline destructor), then the base destructor.
CFigureManager::~CFigureManager() {
    using Node = stlp::hash_map<std::string, FigureModel*, stlp::StringHash>::Node;
    CFigureManager* manager = this;
    auto& table = manager->models_;
    ForEachNode(table, [](Node* node) {
        if (FigureModel* model = node->value) {
            DestroyFigureModel(model);
            gamecrt::operator_delete(model);
        }
    });
    manager->CResourceManager::Destroy();   // the base's clean-up
    table.ClearWith([](Node* n) {
        std::destroy_at(&n->key);
        stlp::deallocate(n, sizeof(Node));
    });
    stlp::vector<Node*>& buckets = Buckets(table);
    if (buckets.Start())
        Deallocate(buckets.Start(), static_cast<std::size_t>(buckets.EndOfStorage() - buckets.Start()) * 4);
}
