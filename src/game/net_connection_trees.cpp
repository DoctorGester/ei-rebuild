// The connection's five STLport maps (net_connection.h), their members registered at the library
// addresses (stlp_tree.h, stlp_tree_abi.h) so the standalone image has them; the connection's
// forwarders keep calling these addresses. Checked by tests/verify/verify_net_connection_trees.cpp.
// Node sizes from each one's _M_create_node: 0x18, the records' 0x1C.
//  - the objects by sent id: signed keys (the exe compares them with JL / JGE), RefPtr values
//    copied with one more reference and released (PairReleaseSecond, 00438A30);
//  - the objects by object and the entries of each priority: pointer keys, the same values
//    (released by 00438A50 / 00438A70);
//  - the records: signed keys, Record values shared (RefPtr::Share) and destroyed with the
//    owner's detach (PairDestroySecond, 00438AE0);
//  - the attached ids: pointer keys, plain 4-byte values.
#include "net_connection_trees.h"

#include "ei/hook.h"

// Members shared by every tree (node pointers only).
EI_REPLACE(0x00439530, stlp::TreeIteratorCopy);
EI_REPLACE(0x00437D60, stlp::TreeIteratorCopy);
EI_REPLACE(0x00437F10, stlp::TreeIteratorCopy);
EI_REPLACE(0x00439B70, stlp::TreeIteratorCopy);
EI_REPLACE(0x00439C40, stlp::TreeIteratorCopy);
EI_REPLACE(0x0043FDF0, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x0043FE10, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x0043FE30, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x0043FE50, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x0043FE70, stlp::TreeInsertResultConstruct);
EI_REPLACE(0x0043E5F0, stlp::TreeDistance);
EI_REPLACE(0x0043EA30, stlp::TreeDistance);
EI_REPLACE(0x0043EEB0, stlp::TreeDistance);
EI_REPLACE(0x0043F5E0, stlp::TreeDistance);
EI_REPLACE(0x00440040, stlp::TreeDistanceTagged);
EI_REPLACE(0x00440090, stlp::TreeDistanceTagged);
EI_REPLACE(0x004400E0, stlp::TreeDistanceTagged);
EI_REPLACE(0x00440130, stlp::TreeDistanceTagged);
EI_REPLACE(0x0043A460, stlp::TreeGetAllocator);
EI_REPLACE(0x0043A4C0, stlp::TreeGetAllocator);
EI_REPLACE(0x0043A520, stlp::TreeGetAllocator);
EI_REPLACE(0x0043A740, stlp::TreeGetAllocator);
EI_REPLACE(0x0043A930, stlp::TreeGetAllocator);
EI_REPLACE(0x0043AD80, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x0043ADA0, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x0043ADC0, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x0043B140, stlp::TreeAllocProxyConstruct);
EI_REPLACE(0x0043B160, stlp::TreeAllocProxyConstruct);

// Each tree's ~_Rb_tree (no direct callers in the exe).
EI_REPLACE(0x00433710, net::trees::SentIds::Destroy);
EI_REPLACE(0x004337B0, net::trees::Objects::Destroy);
EI_REPLACE(0x004338F0, net::trees::Records::Destroy);
EI_REPLACE(0x00433990, net::trees::Attached::Destroy);

// The objects by sent id (0043A480, its node deallocate, is registered by stlp_vector.cpp).
EI_REPLACE(0x004393C0, net::trees::SentIds::ConstructWith);
EI_REPLACE(0x00437AA0, net::trees::SentIds::CopyConstruct);
EI_REPLACE(0x004394A0, net::trees::SentIds::Clear);
EI_REPLACE(0x00439500, net::trees::SentIds::FreeHeader);
EI_REPLACE(0x00437B90, net::trees::SentIds::Subscript);
EI_REPLACE(0x0043BF00, net::trees::SentIds::Find);
EI_REPLACE(0x0043BF60, net::trees::SentIds::LowerBound);
EI_REPLACE(0x0043FD40, net::trees::SentIds::Begin);
EI_REPLACE(0x0043DD50, net::trees::SentIds::CreateNode);
EI_REPLACE(0x0043BA60, net::trees::SentIds::CopySubtree);
EI_REPLACE(0x0043BB70, net::trees::SentIds::EraseSubtree);
EI_REPLACE(0x0043E140, net::trees::SentIds::InsertAt);
EI_REPLACE(0x0043E3A0, net::trees::SentIds::InsertUnique);
EI_REPLACE(0x0043BBC0, net::trees::SentIds::InsertHint);
EI_REPLACE(0x00439390, net::trees::SentIds::InsertHint);
EI_REPLACE(0x0043FD50, net::trees::SentIds::EraseAt);
EI_REPLACE(0x0043E4E0, net::trees::SentIds::EraseRange);
EI_REPLACE(0x0043BE60, net::trees::SentIds::EraseKey);

// The objects by object (0043A4E0 registered by stlp_vector.cpp).
EI_REPLACE(0x00439570, net::trees::Objects::ConstructWith);
EI_REPLACE(0x00437C50, net::trees::Objects::CopyConstruct);
EI_REPLACE(0x004396A0, net::trees::Objects::Clear);
EI_REPLACE(0x00439700, net::trees::Objects::FreeHeader);
EI_REPLACE(0x0043C450, net::trees::Objects::Find);
EI_REPLACE(0x0043C4B0, net::trees::Objects::LowerBound);
EI_REPLACE(0x0043C3A0, net::trees::Objects::Begin);
EI_REPLACE(0x0043DDE0, net::trees::Objects::CreateNode);
EI_REPLACE(0x0043BFA0, net::trees::Objects::CopySubtree);
EI_REPLACE(0x0043C0B0, net::trees::Objects::EraseSubtree);
EI_REPLACE(0x0043E620, net::trees::Objects::InsertAt);
EI_REPLACE(0x0043E7E0, net::trees::Objects::InsertUnique);
EI_REPLACE(0x0043C100, net::trees::Objects::InsertHint);
EI_REPLACE(0x00439540, net::trees::Objects::InsertHint);
EI_REPLACE(0x00439650, net::trees::Objects::EraseAt);
EI_REPLACE(0x00437D40, net::trees::Objects::EraseAt);
EI_REPLACE(0x0043E920, net::trees::Objects::EraseRange);
EI_REPLACE(0x0043C3B0, net::trees::Objects::EraseKey);
EI_REPLACE(0x0043A050, net::trees::Objects::MakeValue);

// The entries of each priority.
EI_REPLACE(0x00437D70, net::trees::Entries::Construct);
EI_REPLACE(0x00437E00, net::trees::Entries::CopyConstruct);
EI_REPLACE(0x00433850, net::trees::Entries::Destroy);
EI_REPLACE(0x004397B0, net::trees::Entries::FreeHeader);
EI_REPLACE(0x0043EE30, net::trees::Entries::DeallocateNodes);
EI_REPLACE(0x0043C9A0, net::trees::Entries::Find);
EI_REPLACE(0x0043CA00, net::trees::Entries::LowerBound);
EI_REPLACE(0x0043C8F0, net::trees::Entries::Begin);
EI_REPLACE(0x0043DE70, net::trees::Entries::CreateNode);
EI_REPLACE(0x0043C4F0, net::trees::Entries::CopySubtree);
EI_REPLACE(0x0043C600, net::trees::Entries::EraseSubtree);
EI_REPLACE(0x0043EA60, net::trees::Entries::InsertAt);
EI_REPLACE(0x0043EC20, net::trees::Entries::InsertUnique);
EI_REPLACE(0x0043C650, net::trees::Entries::InsertHint);
EI_REPLACE(0x00439730, net::trees::Entries::InsertHint);
EI_REPLACE(0x00439760, net::trees::Entries::EraseAt);
EI_REPLACE(0x00437EF0, net::trees::Entries::EraseAt);
EI_REPLACE(0x0043ED60, net::trees::Entries::EraseRange);
EI_REPLACE(0x0043C900, net::trees::Entries::EraseKey);
EI_REPLACE(0x0043A070, net::trees::Entries::MakeValue);

// The records.
EI_REPLACE(0x0043A590, net::trees::Records::ConstructBase);
EI_REPLACE(0x0043A570, net::trees::Records::EmptyInitialize);
EI_REPLACE(0x004381B0, net::trees::Records::CopyConstruct);
EI_REPLACE(0x00439AE0, net::trees::Records::Clear);
EI_REPLACE(0x004383A0, net::trees::Records::Destroy);
EI_REPLACE(0x00439B40, net::trees::Records::FreeHeader);
EI_REPLACE(0x0043B280, net::trees::Records::DeallocateNodesFast);
EI_REPLACE(0x004382A0, net::trees::Records::Subscript);
EI_REPLACE(0x0043CE70, net::trees::Records::Find);
EI_REPLACE(0x00439AA0, net::trees::Records::LowerBound);
EI_REPLACE(0x0043CED0, net::trees::Records::LowerBound);
EI_REPLACE(0x0043CE60, net::trees::Records::Begin);
EI_REPLACE(0x0043DF00, net::trees::Records::CreateNode);
EI_REPLACE(0x0043CA80, net::trees::Records::CopySubtree);
EI_REPLACE(0x0043CB70, net::trees::Records::EraseSubtree);
EI_REPLACE(0x0043EEE0, net::trees::Records::InsertAt);
EI_REPLACE(0x0043F0A0, net::trees::Records::InsertUnique);
EI_REPLACE(0x0043CBC0, net::trees::Records::InsertHint);
EI_REPLACE(0x00439A70, net::trees::Records::InsertHint);
EI_REPLACE(0x0043A090, net::trees::Records::MakeValue);

// The attached ids.
EI_REPLACE(0x0043A780, net::trees::Attached::ConstructBase);
EI_REPLACE(0x0043A760, net::trees::Attached::EmptyInitialize);
EI_REPLACE(0x00438440, net::trees::Attached::CopyConstruct);
EI_REPLACE(0x00439BB0, net::trees::Attached::Clear);
EI_REPLACE(0x004385A0, net::trees::Attached::Destroy);
EI_REPLACE(0x00439C10, net::trees::Attached::FreeHeader);
EI_REPLACE(0x0043F5A0, net::trees::Attached::DeallocateNodes);
EI_REPLACE(0x0043B2C0, net::trees::Attached::DeallocateNodesFast);
EI_REPLACE(0x00438530, net::trees::Attached::Subscript);
EI_REPLACE(0x0043D380, net::trees::Attached::Find);
EI_REPLACE(0x0043D3E0, net::trees::Attached::LowerBound);
EI_REPLACE(0x0043FDA0, net::trees::Attached::Begin);
EI_REPLACE(0x0043E020, net::trees::Attached::CreateNode);
EI_REPLACE(0x0043CF10, net::trees::Attached::CopySubtree);
EI_REPLACE(0x0043D000, net::trees::Attached::EraseSubtree);
EI_REPLACE(0x0043F1E0, net::trees::Attached::InsertAt);
EI_REPLACE(0x0043F3A0, net::trees::Attached::InsertUnique);
EI_REPLACE(0x0043D040, net::trees::Attached::InsertHint);
EI_REPLACE(0x00439B80, net::trees::Attached::InsertHint);
EI_REPLACE(0x0043FDB0, net::trees::Attached::EraseAt);
EI_REPLACE(0x0043F4E0, net::trees::Attached::EraseRange);
EI_REPLACE(0x0043D2E0, net::trees::Attached::EraseKey);
