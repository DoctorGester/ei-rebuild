#pragma once

// What the database loaders share (units, skills, spells, items, prints, acks): the archive and the memory
// stream on their stacks, the open of a folder's database and a table's pass over the
// stream.

#include "engine/data/chunk_archive.h"
#include "engine/core/core_streams.h"
#include "lib/crt.h"
#include "engine/data/resource.h"

#include <string>


namespace dbfile {

// The loads that take only a folder (quests, prints, acks) open `folder` + "database.res" (a missing one ends the
// game).
inline void OpenDatabase(MappedResource& resource, const std::string& folder) {
    resource.OpenResource(folder + "database.res");
}

// One table's pass over the stream: an archive of its own opened for loading, `read(archive)` reads the table's
// records (the archive's serialiser of the table's chunk), the archive closed.
template <typename Read>
void LoadTable(MemoryStream* stream, Read read) {
    ChunkArchive archive;
    archive.Open(stream, 1, 1);
    read(archive);
    archive.Close(stream);
}

} // namespace dbfile
