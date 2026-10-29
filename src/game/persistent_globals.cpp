// Rebirth's PersistentGlobalVars (addon.dll 1001E3C1), reproduced behind its flag: where the game empties a
// variable table, the "gvar." entries survive. addon copies them into an std::unordered_map<std::string,
// float> of its own compiler (MSVC 2010: FNV-1 over every (len / 10 + 1)-th character, 8 buckets growing by 8
// at a load factor above 1, nodes kept in one list with each bucket's range in it), lets the game clear the
// table, then sets the entries again in the order of that list.
// More: docs/notes/comments-c22i.md, "persistent_globals.cpp".
#include "object_tables.h"

#include "ei/hook.h"

#include <cstring>
#include <iterator>
#include <list>
#include <string>
#include <vector>
#include <bit>

namespace {

// MSVC 2010's std::unordered_map<std::string, float>, insertion order only: the element list, two list
// positions per bucket (first and last of its range; end() when empty), mask and maximum index.
class AddonStringMap {
public:
    struct Element {
        std::string key;
        float value;
    };

    AddonStringMap() { Init(kMinBuckets); }

    // operator[]-style: the element for `key`, inserted with 0 when missing (at the front of the
    // list, then moved into its bucket by _Insert).
    Element& Find(const std::string& key) {
        std::size_t bucket = Bucket(key);
        for (std::list<Element>::iterator it = Begin(bucket); it != End(bucket); ++it)
            if (it->key == key) return *it;
        list.push_front(Element{key, 0.0f});
        std::list<Element>::iterator node = list.begin();
        Insert(node);
        return *node;
    }

    const std::list<Element>& Elements() const { return list; }

private:
    static constexpr std::size_t kMinBuckets = 8;

    // std::hash<std::string> of MSVC 2010: FNV-1 (multiply, then XOR the sign-extended
    // character) over every stride-th character.
    static std::size_t Hash(const std::string& key) {
        std::size_t value = 2166136261u;
        std::size_t stride = 1 + key.size() / 10;
        for (std::size_t i = 0; i < key.size(); i += stride)
            value = 16777619u * value ^ static_cast<std::size_t>(static_cast<signed char>(key[i]));
        return value;
    }
    std::size_t Bucket(const std::string& key) const {
        std::size_t n = Hash(key) & mask;
        if (maxIndex <= n) n -= (mask >> 1) + 1;
        return n;
    }
    void Init(std::size_t buckets) {
        vec.assign(buckets * 2, list.end());
        mask = buckets - 1;
        maxIndex = buckets;
    }
    std::list<Element>::iterator Begin(std::size_t bucket) { return vec[2 * bucket]; }
    std::list<Element>::iterator End(std::size_t bucket) {
        if (vec[2 * bucket] == list.end()) return list.end();
        return std::next(vec[2 * bucket + 1]);
    }

    // _Insert: `node` is already in the list. The bucket is scanned from its end; a new key ends
    // up in front of the bucket's first element, or at the end of the list for an empty bucket.
    bool Insert(std::list<Element>::iterator node) {
        std::size_t bucket = Bucket(node->key);
        std::list<Element>::iterator where = End(bucket);
        while (where != Begin(bucket)) {
            --where;
            if (where->key != node->key) continue;   // still too high in the bucket
            list.erase(node);                        // an equal key: the new node is discarded
            return false;
        }
        if (where != std::next(node)) list.splice(where, list, node);
        InsertBucket(node, where, bucket);
        CheckSize();
        return true;
    }
    void InsertBucket(std::list<Element>::iterator node, std::list<Element>::iterator where, std::size_t bucket) {
        if (vec[2 * bucket] == list.end()) {
            vec[2 * bucket] = node;
            vec[2 * bucket + 1] = node;
        } else if (vec[2 * bucket] == where) {
            vec[2 * bucket] = node;
        } else if (++vec[2 * bucket + 1] != node) {
            --vec[2 * bucket + 1];
        }
    }
    // A load factor above 1 (computed as float) multiplies the buckets by 8 and reinserts every
    // element in list order.
    void CheckSize() {
        std::size_t buckets = vec.size() / 2;
        if (static_cast<float>(list.size()) / static_cast<float>(buckets) <= 1.0f) return;
        for (int i = 0; i < 3 && buckets < 0x1FFFFFFF; ++i) buckets *= 2;
        Init(buckets);
        if (list.empty()) return;
        std::list<Element>::iterator last = std::prev(list.end());
        for (;;) {
            std::list<Element>::iterator first = list.begin();
            bool done = first == last;
            Insert(first);
            if (done) break;
        }
    }

    std::list<Element> list;
    std::vector<std::list<Element>::iterator> vec;
    std::size_t mask = 0;
    std::size_t maxIndex = 0;
};

} // namespace

void ClearVariablesKeepingGlobals(NamedFloatTable* table) {
    AddonStringMap kept;
    // addon's iterator: buckets in order, each chain from its head.
    for (const auto& [name, value] : *table) {
        if (!name.starts_with("gvar.")) continue;
        kept.Find(name).value = value;
    }
    table->clear();
    for (const AddonStringMap::Element& e : kept.Elements()) table->Set(e.key, e.value);
}
