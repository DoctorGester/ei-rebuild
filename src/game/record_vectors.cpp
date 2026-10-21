// The game's STLport vector<T> instantiations over game records (record_vectors.h), registered at
// their own addresses; the forwarders in the area files keep calling these addresses. The element
// size of each was read from its size arithmetic. Checked in game by verify_record_vectors.cpp until S21d.
#include "record_vectors.h"

#include "ei/hook.h"

using namespace recordvec;

// The game flow's zone table: _M_insert_overflow (RET 0xC).
EI_REPLACE(0x00583920, ZoneTableVector::Destruct);   // S3c: the zone table 007AFDE0 at exit (0057F4A0)
