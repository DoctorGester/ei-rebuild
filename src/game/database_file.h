#pragma once

// What the database loaders share (units, skills, spells, items, prints, acks): the archive and the memory
// stream on their stacks (resource_local.h), the open of a folder's database and a table's pass over the
// stream.

#include "chunk_archive.h"
#include "core_streams.h"
#include "lib/crt.h"
#include "resource_local.h"

#include <string>


namespace dbfile {

// The loads that take only a folder (quests, prints, acks) open `folder` + "database.res"; false
// when it cannot be opened.
inline bool OpenDatabase(LocalResource& resource, const std::string& folder) {
    return resource.Open((folder + "database.res")) != 0;
}
// Their report of a failed open. The game names "databases.res" here.
inline int ReportNoDatabase(const std::string& folder) {
    return game::FatalError("Can't open database file \"%s\"\n", (folder + "databases.res"));
}

// One table's pass over the stream: an archive of its own (the out-of-line constructor)
// opened for loading, the table's records read as chunk `id` by the archive's vector serialiser
// `serialize` (the chunk id, the vector), the archive closed; `index` then
// files the records, and the archive is destroyed.
template <typename Records, typename Table, typename Index>
void LoadTable(MemoryStream* stream, int id, void (ChunkArchive::*serialize)(int, Records*), Table* table, Index index) {
    ChunkArchive archive;
    archive.Open(stream, 1, 1);
    (archive.*serialize)(id, table);
    archive.Close(stream);
    index();
}

} // namespace dbfile
