#pragma once

// STLport sort instantiations over game elements, registered at their library addresses
// (stl_sorts.cpp, the template in lib/stlp_algo.h). The callers keep their forwarders; first and
// last come in ECX / EDX, then the null value-type tag, the depth limit (twice the size's log2) and
// the comparison object by value. Each is fastcall with the original's stack cleanup.

#include "net_classes.h"
#include "ui_screens.h"
#include "world.h"

#include <cstdint>

namespace net {
class FileChunkMessage;
}
namespace mfc {
class CString;
}
struct SaveEntry;

namespace stl_sorts {

// The client's file chunks (net_client_handlers.cpp), by their index (+0x10, unsigned 16 bits).
// 0042A020 (RET 0xC) __introsort_loop, 0042A150 (RET 4) __final_insertion_sort.
void __fastcall ChunkIntroSort(net::FileChunkMessage** first, net::FileChunkMessage** last, void* tag, int depth, int comp);
void __fastcall ChunkFinalSort(net::FileChunkMessage** first, net::FileChunkMessage** last, int comp);

// CollectChanges's {object, priority} pairs (net_connection.cpp), by the object's address.
// 0043F870 (RET 0xC) __introsort_loop, 0043F9B0 (RET 4) __final_insertion_sort.
void __fastcall ReplicatedIntroSort(net::Replicated* first, net::Replicated* last, void* tag, int depth, void* comp);
void __fastcall ReplicatedFinalSort(net::Replicated* first, net::Replicated* last, void* comp);

// The picking hits (world_client_maps.cpp), front to back with DepthLess {plane, manager} (by
// value: two dwords). 00573740 (RET 0x10) __introsort_loop, 00575BC0 (RET 8) __insertion_sort,
// 00576D40 (RET 0xC) __unguarded_insertion_sort. The original inlines the comparison in some
// scans; its depth sums are the same (only the first two terms trade places).
void __fastcall ScreenIntroSort(ScreenEntry* first, ScreenEntry* last, void* tag, int depth, const float* plane,
                                void* manager);
void __fastcall ScreenInsertionSort(ScreenEntry* first, ScreenEntry* last, const float* plane, void* manager);
void __fastcall ScreenUnguardedInsertionSort(ScreenEntry* first, ScreenEntry* last, void* tag, const float* plane,
                                             void* manager);

// The battle screen's party ids (ui_battle_update.cpp), less<int>. 0060B1E0 (RET 8)
// __introsort_loop (no comparison object), 0064B750 (plain RET) __final_insertion_sort.
void __fastcall IdIntroSort(int* first, int* last, const int* tag, int depth);
void __fastcall IdFinalSort(int* first, int* last);

// The connection screen's server list (ui_connect_screen.cpp) by the sort key (CMpConnectScreen's
// sortKey_, by value): 1 / 2 the name ascending / descending (_mbscmp), 3 / 4 the players, 5 / 6 the
// game type, 7 / 8 the ping, anything else the state, ascending. 0061EF60 (RET 0xC)
// __introsort_loop, 0061F6F0 (RET 4) __insertion_sort, 0061FB00 (RET 8)
// __unguarded_insertion_sort. The copies only share the strings (no allocation).
void __fastcall ServerIntroSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry* tag, int depth, int key);
void __fastcall ServerInsertionSort(MpAddressEntry* first, MpAddressEntry* last, int key);
void __fastcall ServerUnguardedInsertionSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry* tag, int key);

// The objectives window's quests (ui_objectives.cpp, the names of the shown zone's "q.<zone>.<quest>"
// variables) by their quest record's order (+0x1C), the highest first and a name without a record
// before all (also before itself: the original's scans run off the range then). Every comparison
// looks both names up in the quest table 007C87B4 (QuestTable::Find, which adds a null entry for a
// new name), the first argument's first: the original inlines that lookup in each helper (a string
// key, a {key, null} pair, find_or_insert, the pair and the key freed), every copy allocating
// alike. Copies of the CStrings only share them. 0064F310 (RET 0xC) __introsort_loop, 0064F9A0
// (RET 4) __insertion_sort, 0064FBD0 (RET 8) __unguarded_insertion_sort.
void __fastcall QuestIntroSort(mfc::CString* first, mfc::CString* last, void* tag, int depth, int comp);
void __fastcall QuestInsertionSort(mfc::CString* first, mfc::CString* last, int comp);
void __fastcall QuestUnguardedInsertionSort(mfc::CString* first, mfc::CString* last, void* tag, int comp);

// The save / load list (ui_saveload.cpp) by the saved time (+0x08, signed; comparison 00658020),
// the oldest first. A SaveEntry copy allocates (its CStringArrays), so every copy, assignment and
// destruction is STLport's (stlp::Held, SaveEntryOps): the copy constructor 00656640, operator=
// 006538E0 (inlined in the helpers, the same calls) and the destructor 00656720. 00657DF0 (RET 0xC)
// __introsort_loop, 00657F10 (RET 4) __final_insertion_sort; 00657D50 (RET 4) copy_backward
// (first, last, result end), returning the first element written.
void __fastcall SaveIntroSort(SaveEntry* first, SaveEntry* last, void* tag, int depth, int comp);
void __fastcall SaveFinalSort(SaveEntry* first, SaveEntry* last, int comp);
SaveEntry* __fastcall SaveCopyBackward(SaveEntry* first, SaveEntry* last, SaveEntry* result);

} // namespace stl_sorts
