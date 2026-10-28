// lib::HashMap (src/lib/hash_map.h): STLport's walk order on cases worked out by hand. Not recorded vectors.
#include "../../src/lib/hash_map.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Map = lib::HashMap<std::uint32_t, int>;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("ei_vectors: hash map: %s\n", what);
}

std::vector<std::uint32_t> Walk(const Map& map) {
    std::vector<std::uint32_t> keys;
    for (const auto& [key, value] : map) keys.push_back(key);
    return keys;
}
std::vector<std::uint32_t> Chain(const Map& map, std::size_t bucket) {
    std::vector<std::uint32_t> keys;
    for (auto n = map.begin(bucket); n != map.end(bucket); ++n) keys.push_back(n->first);
    return keys;
}

// 193 buckets; a new key goes to the head of its bucket's chain, buckets walked in order.
void Order() {
    Map map;
    map[1] = 1;
    map[194] = 2;
    map[2] = 3;
    Check(map.bucket_count() == 193, "100 asks for 193 buckets");
    Check(Walk(map) == std::vector<std::uint32_t>{194, 1, 2}, "a new key at the head of its chain");
    Map copy(map);
    Check(Walk(copy) == Walk(map) && copy.bucket_count() == 193, "a copy keeps the order");
    auto next = map.erase(map.begin());
    Check(next != map.end() && next->first == 1 && Walk(map) == std::vector<std::uint32_t>{1, 2},
          "erase gives the next in walk order");
    map.clear();
    Check(map.empty() && map.bucket_count() == 193, "clear keeps the buckets");
}

// 53 buckets; the 54th insert grows the table to 97 first, each old chain moved head first, so 0 (old bucket 0)
// and 97 (old bucket 44), which share new bucket 0, come out reversed.
void Growth() {
    Map map(53);
    map[0] = 0;
    map[97] = 1;
    for (std::uint32_t k = 1; k <= 51; ++k) map[k] = 2;
    Check(map.bucket_count() == 53 && map.size() == 53, "53 keys in 53 buckets");
    Check(Chain(map, 0) == std::vector<std::uint32_t>{0}, "0 alone in bucket 0");
    map[52] = 3;
    Check(map.bucket_count() == 97 && map.size() == 54, "the 54th key grows the table to 97");
    Check(Chain(map, 0) == std::vector<std::uint32_t>{97, 0}, "the moved chains reversed");
    map[0] = 4;
    Check(map.size() == 54 && map.find(0)->second == 4, "operator[] on a known key");
}

// hash_multimap: an equal key right after the first entry with it, a new key at the head; equal_range ends at the
// entry after the equal ones in walk order.
void Multi() {
    lib::HashMultimap<std::uint32_t, int> map;
    map.insert({1, 1});
    map.insert({194, 2});
    map.insert({1, 3});
    map.insert({1, 4});
    map.insert({2, 5});
    std::vector<int> values;
    for (const auto& [key, value] : map) values.push_back(value);
    Check(values == std::vector<int>{2, 1, 4, 3, 5}, "equal keys after the first one with it");
    auto [first, last] = map.equal_range(1);
    Check(first->second == 1 && last != map.end() && last->first == 2, "equal_range ends at the next bucket's head");
    auto [none, alsoNone] = map.equal_range(3);
    Check(none == map.end() && alsoNone == map.end(), "equal_range of a missing key");
}

void Text() {
    lib::TextHash hash;
    Check(hash("ab") == 5u * 'a' + 'b', "h = 5 * h + c");
    Check(hash("\xFF") == 0xFFFFFFFFu, "signed characters");
}

} // namespace

// replay_main.cpp: the number of failed checks.
int RunHashMapChecks() {
    g_checks = g_failures = 0;
    Order();
    Growth();
    Multi();
    Text();
    std::printf("ei_vectors: hash map: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
