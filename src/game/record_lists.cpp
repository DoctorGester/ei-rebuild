// The game's STLport list<T> instantiations over game records (record_lists.h), registered at their
// own addresses; the forwarders in the area files keep calling these addresses. Checked in game by
// verify_record_lists.cpp until S21d.
#include "record_lists.h"

#include "ei/hook.h"

using namespace recordlist;

EI_REWRITTEN(0x0062C090, TitleLines::Insert);
EI_REWRITTEN(0x0056ED90, BloodPrints::Insert);
EI_REWRITTEN(0x0056EED0, Footprints::Insert);
EI_REWRITTEN(0x0056F010, ScorchMarks::Insert);
EI_REWRITTEN(0x004FB2E0, LinkArchives::InsertDefault);
EI_REWRITTEN(0x00478FF0, StreamQueue::InsertDefault);
EI_REWRITTEN(0x004790F0, StreamQueue::Erase);
EI_REWRITTEN(0x00479570, StreamQueue::Clear);
EI_REWRITTEN(0x004795B0, StreamQueue::InsertRange);
EI_REWRITTEN(0x00424900, Trades::InsertDefault);
EI_REWRITTEN(0x00424A40, Trades::Destruct);
EI_REWRITTEN(0x00429770, Trades::Erase);
EI_REWRITTEN(0x0040A8D0, ChunkStack::InsertDefault);
EI_REWRITTEN(0x0040A6B0, ChunkStack::PushBackDefault);
EI_REWRITTEN(0x0040A6D0, ChunkStack::PopBack);
