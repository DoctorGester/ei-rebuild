#pragma once

// What the database loaders share (units, skills, spells, items, prints, acks): the archive and the memory
// stream on their stacks, the open of a folder's database and a table's pass over the
// stream.

#include "chunk_archive.h"
#include "core_streams.h"
#include "lib/crt.h"
#include "resource.h"

#include <string>


namespace dbfile {

// The loads that take only a folder (quests, prints, acks) open `folder` + "database.res"; false
// when it cannot be opened.
inline bool OpenDatabase(MappedResource& resource, const std::string& folder) {
    return resource.OpenResource(folder + "database.res");
}
// Their report of a failed open. The game names "databases.res" here.
inline int ReportNoDatabase(const std::string& folder) {
    return game::FatalError("Can't open database file \"%s\"\n", (folder + "databases.res"));
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
