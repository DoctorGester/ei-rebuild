// The game glue's small functions (game.exe 0x00578000-0x00578E30). See world_glue.h and
// docs/notes/world.md.
#include "world_glue.h"

#include "world_data.h"

#include "ei/hook.h"

const mfc::CString* GameDataPathString() {
    return static_cast<const mfc::CString*>(worlddata::DataFolderString());
}
EI_REPLACE(0x00578DB0, GameDataPathString);

const mfc::CString* GameBasePathString() {
    return static_cast<const mfc::CString*>(worlddata::BaseFolderString());
}
EI_REPLACE(0x00578DC0, GameBasePathString);
