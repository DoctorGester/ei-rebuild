// 005A9C90 (thiscall): the figure model's destructor (FigureModel, model.h). Every cached animation
// is deleted (slot 0 with 1), the map cleared (each node's pair destroyed, the node returned to the
// STLport node allocator), each archive closed (CloseResource, slot 3) and deleted, then the map's
// own destructor runs inline: a second clear (empty by then) and the buckets freed. See
// docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel

#include "record_lifetime.h"

#include "lib/crt.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstdint>

namespace {

// The bucket vector of one of the two maps (STLport's hashtable keeps it after the functors).
template <typename Map>
stlp::vector<typename Map::Node*>& Buckets(Map& table) {
    return *static_cast<stlp::vector<typename Map::Node*>*>(table.BucketVector());
}

// 004298D0: the string's hash (thiscall). 005AE5B0 (RET 4): destroys a node's pair.
__declspec(naked) std::uint32_t __fastcall HashString(const stlp::string*) EI_ORIGINAL(0x004298D0)
__declspec(naked) void __fastcall DestroyPair(void*, void*, int) EI_ORIGINAL(0x005AE5B0)
// 00404D70: STLport deallocate(p, bytes). 005AE150 (RET 8): the node allocator's deallocate(p, n).
void __fastcall Deallocate(void* a0, std::size_t a1) { StlNodeFreeOriginal(a0, a1); }
__declspec(naked) void __fastcall NodeDeallocate(void*, void*, FigureModel::AnimationMap::Node*, int) EI_ORIGINAL(0x005AE150)
// 00406590: deallocates `count` pointers (the buckets).
void __fastcall DeallocatePointers(void* a0, int a1) { BucketsDeallocateOriginal(a0, (std::size_t)a1); }

// The figure manager's helpers: the model destructor (through its original address), 00674F40
// (CResourceManager::Destroy, the base's clean-up), 005AE5D0 (RET 4) the pair destructor, 004FC080
// (ECX node, EDX count) the node allocator's deallocate, 00674DF0 the base destructor.
__declspec(naked) void __fastcall ManagerCleanUp(CFigureManager*) EI_ORIGINAL(0x00674F40)
__declspec(naked) void __fastcall DestroyNamePair(void*, void*, int) EI_ORIGINAL(0x005AE5D0)
__declspec(naked) void __fastcall NodeFree(void*, int) EI_ORIGINAL(0x004FC080)
__declspec(naked) void __fastcall ManagerBaseDestroy(CFigureManager*) EI_ORIGINAL(0x00674DF0)

// Visits every node as the game's iterator walks them: the chain, then the head of the next
// non-empty bucket after the one the node's key hashes to (004298D0).
template <typename Map, typename F>
void ForEachNode(Map& table, F visit) {
    typename Map::Node* node = table.First();
    while (node) {
        visit(node);
        if (node->next) {
            node = node->next;
            continue;
        }
        std::uint32_t b = HashString(&node->key) % table.BucketCount();
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
        DestroyPair(&n->key, nullptr, 0);
        if (n) Deallocate(n, sizeof(Node));
    });

    static constexpr CMappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (CMappedResource* FigureModel::*archive : kArchives) (model->*archive)->CloseResource();
    for (CMappedResource* FigureModel::*archive : kArchives)
        if (model->*archive) delete (model->*archive);

    // The hash_map's destructor.
    table.ClearWith([&table](Node* n) {
        DestroyPair(&n->key, nullptr, 0);
        NodeDeallocate(table.CountProxy(), nullptr, n, 1);
    });
    stlp::vector<Node*>& buckets = Buckets(table);
    DeallocatePointers(buckets.Start(), static_cast<int>(buckets.EndOfStorage() - buckets.Start()));
}

EI_REPLACE(0x005A9C90, DestroyFigureModel);
EI_REPLACE(0x005AE5B0, records::LeadingStringDelete);   // S13
EI_REPLACE(0x005AE5D0, records::LeadingStringDelete);

// 005AA1C0 (thiscall): the figure manager's destructor: its vtable (0073FB88), every cached model
// destroyed and freed, the base's clean-up (00674F40), the model map cleared and its buckets
// freed (the map's inline destructor), then the base destructor (00674DF0). The char* parameter is
// what world_deleting_destructors.cpp declares.
void __fastcall DestroyFigureManager(char* object) {
    using Node = stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash>::Node;
    auto* manager = reinterpret_cast<CFigureManager*>(object);
    ei::SetVtable(manager, CFigureManager::kVtableAddress);
    auto& table = manager->models_;
    ForEachNode(table, [](Node* node) {
        if (FigureModel* model = node->value) {
            FigureModelDestructOriginal(model);
            gamecrt::operator_delete(model);
        }
    });
    ManagerCleanUp(manager);
    table.ClearWith([](Node* n) {
        DestroyNamePair(&n->key, nullptr, 0);
        NodeFree(n, 1);
    });
    stlp::vector<Node*>& buckets = Buckets(table);
    if (buckets.Start())
        Deallocate(buckets.Start(), static_cast<std::size_t>(buckets.EndOfStorage() - buckets.Start()) * 4);
    ManagerBaseDestroy(manager);
}
EI_REPLACE(0x005AA1C0, DestroyFigureManager);
