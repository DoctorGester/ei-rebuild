#pragma once

// The save files' container (header, checksum, compressed data) and mission.sav's load.

class ByteStream;
class MemoryStream;

// Reads mission.sav from a file stream (magic 0x102) and restores the world (LoadSave); old versions
// also carry the globals (< 0x52) and the input state (< 0x47). 0 when the file does not read.
int LoadMission(ByteStream* file);

// SaveFileWrite writes the header, from version kVersionChecksum a checksum, then a flag byte and the LZ-compressed data
// (or, when decompressing it does not give the data back, 0 and the raw data). SaveFileRead reads one back
// (versions up to kSaveLastUncompressedVersion uncompressed); false on a bad header or checksum.
void SaveFileWrite(ByteStream* file, unsigned magic, MemoryStream* data);
bool SaveFileRead(MemoryStream* out, unsigned magic, ByteStream* file);

