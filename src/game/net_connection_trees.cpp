// The connection's five STLport maps (net_connection.h), their members registered at the library
// addresses (stlp_tree.h, stlp_tree_abi.h) so the standalone image has them; the connection's
// forwarders keep calling these addresses. Checked in game by verify_net_connection_trees.cpp until S21d.
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

