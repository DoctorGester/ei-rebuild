#pragma once

// ei-convert's game databases (units.udb, perks.pdb, spells.sdb, items.idb, quests.qdb, Levers.ldb; docs/notes/mods.md,
// "Databases as TOML"): each read by the game's loader, written as TOML, read back and compared.

#include <string>
#include <string_view>
#include <vector>

struct RecordFile;

// Whether `lowerName` (an archive entry's name in lower case) is one of the databases.
bool IsDatabase(std::string_view lowerName);

// The round trip of database `lowerName`: `original` read by the game's loader, its TOML written (`wholeFile` makes
// the file's text of it) into `*toml`, the TOML (as `name`.toml) read back, and every record compared by its bytes as
// the record's own serialiser writes them (so every field the format holds, floats by their bits), the tables'
// indexes by name and the indexes the load resolves. Each difference a line of `differences`.
void ConvertDatabase(std::string_view lowerName, const RecordFile& original, const std::string& name,
                     std::string (*wholeFile)(const std::string&), std::string* toml,
                     std::vector<std::string>& differences);
