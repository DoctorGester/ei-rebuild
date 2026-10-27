#pragma once

// STLport sort instantiations over game elements, registered at their library addresses
// (stl_sorts.cpp, the template in lib/stlp_algo.h). First and
// last come in ECX / EDX, then the null value-type tag, the depth limit (twice the size's log2) and
// the comparison object by value. Each is fastcall with the game's stack cleanup.

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
namespace stlp {
class string;
}
struct SaveEntry;

namespace stl_sorts {

// The client's file chunks (net_client_handlers.cpp), by their index (+0x10, unsigned 16 bits).
// __introsort_loop, __final_insertion_sort.
void __fastcall ChunkIntroSort(net::FileChunkMessage** first, net::FileChunkMessage** last, void* tag, int depth, int comp);
void __fastcall ChunkFinalSort(net::FileChunkMessage** first, net::FileChunkMessage** last, int comp);

// CollectChanges's {object, priority} pairs (net_connection.cpp), by the object's address.
// __introsort_loop, __final_insertion_sort.
void __fastcall ReplicatedIntroSort(net::Replicated* first, net::Replicated* last, void* tag, int depth, void* comp);
void __fastcall ReplicatedFinalSort(net::Replicated* first, net::Replicated* last, void* comp);

// The picking hits (world_client_maps.cpp), front to back with DepthLess {plane, manager} (by
// value: two dwords). __introsort_loop, __insertion_sort,
// __unguarded_insertion_sort. The game inlines the comparison in some
// scans; its depth sums are the same (only the first two terms trade places).
void __fastcall ScreenIntroSort(ScreenEntry* first, ScreenEntry* last, void* tag, int depth, const float* plane,
                                CBaseManager* manager);
void __fastcall ScreenInsertionSort(ScreenEntry* first, ScreenEntry* last, const float* plane, CBaseManager* manager);
void __fastcall ScreenUnguardedInsertionSort(ScreenEntry* first, ScreenEntry* last, void* tag, const float* plane,
                                             CBaseManager* manager);

// The battle screen's party ids (ui_battle_update.cpp), less<int>. __introsort_loop
// (no comparison object), __final_insertion_sort.
void __fastcall IdIntroSort(int* first, int* last, const int* tag, int depth);
void __fastcall IdFinalSort(int* first, int* last);

// The connection screen's server list (ui_connect_screen.cpp) by the sort key (CMpConnectScreen's
// sortKey_, by value): 1 / 2 the name ascending / descending (_mbscmp), 3 / 4 the players, 5 / 6 the
// game type, 7 / 8 the ping, anything else the state, ascending.
// __introsort_loop, __insertion_sort, __unguarded_insertion_sort.
// The copies only share the strings (no allocation).
void __fastcall ServerIntroSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry* tag, int depth, int key);
void __fastcall ServerInsertionSort(MpAddressEntry* first, MpAddressEntry* last, int key);
void __fastcall ServerUnguardedInsertionSort(MpAddressEntry* first, MpAddressEntry* last, MpAddressEntry* tag, int key);

// The objectives window's quests (ui_objectives.cpp, the names of the shown zone's "q.<zone>.<quest>"
// variables) by their quest record's order (+0x1C), the highest first and a name without a record
// before all (also before itself: the game's scans run off the range then). Every comparison
// looks both names up in the quest table (QuestDatabase::FindQuest, which adds a null entry for a
// new name), the first argument's first: the game inlines that lookup in each helper (a string
// key, a {key, null} pair, find_or_insert, the pair and the key freed), every copy allocating
// alike. Copies of the CStrings only share them. __introsort_loop, __insertion_sort,
// __unguarded_insertion_sort.
void __fastcall QuestIntroSort(mfc::CString* first, mfc::CString* last, void* tag, int depth, int comp);
void __fastcall QuestInsertionSort(mfc::CString* first, mfc::CString* last, int comp);
void __fastcall QuestUnguardedInsertionSort(mfc::CString* first, mfc::CString* last, void* tag, int comp);

// The save / load list (ui_saveload.cpp) by the saved time (+0x08, signed), the newest first: the
// comparison is !(a < b) (a.savedAt < b.savedAt is the less), so equal times compare true both ways (not a strict
// weak ordering: with two saves of the same time the unguarded scans can leave the range, in the
// game as here). A SaveEntry copy allocates (its CStringArrays), so every copy, assignment and
// destruction is STLport's (stlp::Held, SaveEntryOps): the copy constructor, operator=
// (inlined in the helpers, the same calls) and the destructor. __introsort_loop,
// __final_insertion_sort; copy_backward
// (first, last, result end), returning the first element written.
void __fastcall SaveIntroSort(SaveEntry* first, SaveEntry* last, void* tag, int depth, int comp);
void __fastcall SaveFinalSort(SaveEntry* first, SaveEntry* last, int comp);
SaveEntry* __fastcall SaveCopyBackward(SaveEntry* first, SaveEntry* last, SaveEntry* result);

// A network client's briefing topics (ui_briefing_topics.cpp, a vector<string>) by _stricmp of the
// texts. A copy allocates (string's copy constructor, the swap's string(first, last)), so
// every copy, assignment (assign(first, last)) and destruction is STLport's
// (stlp::StringElement); the partition takes the pivot by value and frees it before it
// returns. The comparison object is empty (the caller passes the person's id in its slot).
// __introsort_loop, __final_insertion_sort.
void __fastcall TopicIntroSort(stlp::string* first, stlp::string* last, int tag, int depth, int comp);
void __fastcall TopicFinalSort(stlp::string* first, stlp::string* last, int comp);

// The multiplayer hero screen's figure names (ui_hero_screen_create.cpp, a vector<CString>) by
// CString's operator< (_mbscmp). Copies only share the strings.
// __introsort_loop (first, last, the null value-type tag, the depth limit; no comparison object),
// __final_insertion_sort.
void __fastcall NameIntroSort(mfc::CString* first, mfc::CString* last, mfc::CString* tag, int depth);
void __fastcall NameFinalSort(mfc::CString* first, mfc::CString* last);

} // namespace stl_sorts
