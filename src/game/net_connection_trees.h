#pragma once

// The connection's five STLport maps as registered instantiations (net_connection_trees.cpp):
// their value policies and stlp::TreeAbi types.

#include "net_connection.h"

#include "lib/stlp_list_refs.h"
#include "lib/stlp_tree_abi.h"

#include <cstdint>

namespace net::trees {

// RefPtr values copied with one more reference and released.
struct CountedValue : stlp::CountedRef {
    using Type = RefPtr;
};
// A record copied as the pair constructor 0043A090 and _M_create_node 0043DF00 copy it: shared,
// then the version; destroyed with the owner's detach.
struct RecordValue {
    using Type = Record;
    static void Copy(Record* to, const Record& from) {
        to->object.Share(from.object.ptr);
        to->version = from.version;
    }
    static void Destroy(Record* p) { p->object.Destroy(); }
};

template <typename K, typename Second>
using PairTree = stlp::TreeAbi<stlp::map<K, typename Second::Type>,
                               stlp::PairElement<typename stlp::map<K, typename Second::Type>::Value, Second>>;

using SentIds = PairTree<std::int32_t, CountedValue>;
using Objects = PairTree<CRefObject*, CountedValue>;
using Entries = PairTree<ReplicaEntry*, CountedValue>;
using Records = PairTree<std::int32_t, RecordValue>;
using Attached = stlp::TreeAbi<stlp::map<CRefObject*, AttachedId>>;

static_assert(sizeof(SentIds::Node) == 0x18 && sizeof(Records::Node) == 0x1C && sizeof(Attached::Node) == 0x18);
static_assert(sizeof(SentIds::Tree) == 0xC && sizeof(Records::Tree) == sizeof(Connection::RecordMap));

} // namespace net::trees
