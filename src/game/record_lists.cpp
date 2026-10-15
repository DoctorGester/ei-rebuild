// The game's STLport list<T> instantiations over game records (record_lists.h), registered at their
// own addresses; the forwarders in the area files keep calling these addresses. Checked by
// tests/verify/verify_record_lists.cpp.
#include "record_lists.h"

#include "ei/hook.h"

using namespace recordlist;

// The chunk archive's stack: insert(pos) (RET 8), push_back() and pop_back().
EI_REPLACE(0x0040A8D0, ChunkStack::InsertDefault);
EI_REPLACE(0x0040A6B0, ChunkStack::PushBackDefault);
EI_REPLACE(0x0040A6D0, ChunkStack::PopBack);

// The client's trade records: insert(pos) (RET 8), ~list() and erase(pos) (RET 8).
EI_REPLACE(0x00424900, Trades::InsertDefault);
EI_REPLACE(0x00424A40, Trades::Destruct);
EI_REPLACE(0x00429770, Trades::Erase);

// The link queue: insert(pos) (RET 8), erase(pos) (RET 8), clear() and insert(pos, first, last)
// (RET 0xC).
EI_REPLACE(0x00478FF0, StreamQueue::InsertDefault);
EI_REPLACE(0x004790F0, StreamQueue::Erase);
EI_REPLACE(0x00479570, StreamQueue::Clear);
EI_REPLACE(0x004795B0, StreamQueue::InsertRange);

// CLinkFigureLoader's archives: insert(pos) (RET 8).
EI_REPLACE(0x004FB2E0, LinkArchives::InsertDefault);

// The ground prints' insert(pos, x) (RET 0xC).
EI_REPLACE(0x0056ED90, BloodPrints::Insert);
EI_REPLACE(0x0056EED0, Footprints::Insert);
EI_REPLACE(0x0056F010, ScorchMarks::Insert);

// The titles' lines: insert(pos, x) (RET 0xC; ECX not read).
EI_REPLACE(0x0062C090, TitleLines::Insert);
