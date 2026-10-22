#pragma once

// EI_IMAGE_TRACE=1 (verify build, the image's runs only; C25): which of game.exe's .rdata and .data bytes the image
// reads. At the harness's start every page of the moved range's file bytes (0073C000-0079C000 in the storage,
// image_data.cpp; the import table's page is left out, every call reads its slots) that holds a nonzero dword is
// made inaccessible; the first access to each such dword is a line of
// image_trace.txt in the run folder ("read" or "write", its game address, the code and the first return addresses
// of ei_hook.dll on the stack), then the instruction is single-stepped with the page open and the page closed again
// while it holds a dword not yet touched. A dword whose first access is a write never needed game.exe's value.
// tools/image_trace.py names the code and groups the reads by object; the zero-filled trial (gen_image_cells.py
// --zero, config/image_kept.csv) is what proves a list of them is enough.
//
// Cost: every access to a closed page traps, also to its dwords already seen; a page that traps more than
// kTrapLimit times is left open (a line says so, with the dwords it had not seen yet): the script's keyword table
// (0078B000-0078DFFF) and the CRT's character tables are such pages, so the trial covers what the trace misses there.
// A system call that reads or writes a closed page fails instead of trapping: WriteFile's import slot notes and opens
// the buffer's pages around the call (the saves write a one-byte constant of .data).

namespace ei::verify::image_trace {

bool Enabled();   // EI_IMAGE_TRACE=1 in the standalone image
void Install();

} // namespace ei::verify::image_trace
