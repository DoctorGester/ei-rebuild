// Rebirth's PersistentGlobalVars (addon.dll 1001E3C1), reproduced behind its flag: where the game
// empties a variable table, the "gvar." entries survive. addon copies them into an
// std::unordered_map<std::string, float> of its own compiler (MSVC 2010: FNV-1 over every
// (len / 10 + 1)-th character, 8 buckets growing by 8 at a load factor above 1, nodes kept in one
// list with each bucket's range in it), lets the game clear the table (00461540), then sets the
// entries again (00460BC0) in the order of that list. The order decides the order of the entries
// in the game's hash chains (and so in saves and in listings), so the container is reproduced
// here exactly: AddonStringMap below is that unordered_map's insert, find and rehash.
// See docs/notes/rebirth.md ("PersistentGlobalVars").
#include "object_tables.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <cstring>
#include <iterator>
#include <list>
#include <string>
#include <vector>
#include <bit>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {

// 00461540 (clear, FloatTableClearOriginal) and 00460BC0 (Set, RET 8: name, value) of the game's table, through
// their addresses.
void __fastcall TableSet(NamedFloatTable* a0, void*, const char* a2, float a3) { NamedTableSet((void*)a0, nullptr, a2, std::bit_cast<std::uint32_t>(a3)); }

// MSVC 2010's std::unordered_map<std::string, float>, insertion order only: the element list,
// two list positions per bucket (first and last of its range; end() when empty), mask and
// maximum index.
class AddonStringMap {
public:
    struct Element {
        std::string key;
        float value;
    };
    using Iterator = std::list<Element>::iterator;

    AddonStringMap() { Init(kMinBuckets); }

    // operator[]-style: the element for `key`, inserted with 0 when missing (at the front of the
    // list, then moved into its bucket by _Insert).
    Element& Find(const std::string& key) {
        std::size_t bucket = Bucket(key);
        for (Iterator it = Begin(bucket); it != End(bucket); ++it)
            if (it->key == key) return *it;
        list_.push_front(Element{key, 0.0f});
        Iterator node = list_.begin();
        Insert(node);
        return *node;
    }

    const std::list<Element>& Elements() const { return list_; }

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
        std::size_t n = Hash(key) & mask_;
        if (maxIndex_ <= n) n -= (mask_ >> 1) + 1;
        return n;
    }
    void Init(std::size_t buckets) {
        vec_.assign(buckets * 2, list_.end());
        mask_ = buckets - 1;
        maxIndex_ = buckets;
    }
    Iterator Begin(std::size_t bucket) { return vec_[2 * bucket]; }
    Iterator End(std::size_t bucket) {
        if (vec_[2 * bucket] == list_.end()) return list_.end();
        return std::next(vec_[2 * bucket + 1]);
    }

    // _Insert: `node` is already in the list. The bucket is scanned from its end; a new key ends
    // up in front of the bucket's first element, or at the end of the list for an empty bucket.
    bool Insert(Iterator node) {
        std::size_t bucket = Bucket(node->key);
        Iterator where = End(bucket);
        while (where != Begin(bucket)) {
            --where;
            if (where->key != node->key) continue;   // still too high in the bucket
            list_.erase(node);                       // an equal key: the new node is discarded
            return false;
        }
        if (where != std::next(node)) list_.splice(where, list_, node);
        InsertBucket(node, where, bucket);
        CheckSize();
        return true;
    }
    void InsertBucket(Iterator node, Iterator where, std::size_t bucket) {
        if (vec_[2 * bucket] == list_.end()) {
            vec_[2 * bucket] = node;
            vec_[2 * bucket + 1] = node;
        } else if (vec_[2 * bucket] == where) {
            vec_[2 * bucket] = node;
        } else if (++vec_[2 * bucket + 1] != node) {
            --vec_[2 * bucket + 1];
        }
    }
    // A load factor above 1 (computed as float) multiplies the buckets by 8 and reinserts every
    // element in list order.
    void CheckSize() {
        std::size_t buckets = vec_.size() / 2;
        if (!(static_cast<float>(list_.size()) / static_cast<float>(buckets) > 1.0f)) return;
        for (int i = 0; i < 3 && buckets < 0x1FFFFFFF; ++i) buckets *= 2;
        Init(buckets);
        if (list_.empty()) return;
        Iterator last = std::prev(list_.end());
        for (;;) {
            Iterator first = list_.begin();
            bool done = first == last;
            Insert(first);
            if (done) break;
        }
    }

    std::list<Element> list_;
    std::vector<Iterator> vec_;
    std::size_t mask_ = 0;
    std::size_t maxIndex_ = 0;
};

} // namespace

void ClearVariablesKeepingGlobals(NamedFloatTable* table) {
    AddonStringMap kept;
    // addon's iterator: buckets in order, each chain from its head.
    for (NamedFloatTable::Node** bucket = table->buckets; bucket < table->finish; ++bucket) {
        for (NamedFloatTable::Node* node = *bucket; node; node = node->next) {
            if (std::strncmp(node->key, "gvar.", 5) != 0) continue;
            kept.Find(node->key).value = node->value;
        }
    }
    FloatTableClearOriginal(table);
    for (const AddonStringMap::Element& e : kept.Elements()) TableSet(table, nullptr, e.key.c_str(), e.value);
}
