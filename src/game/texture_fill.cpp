// Loading a .bmp texture with its alpha file (00690260). (The fill of a texture's system-memory
// surface from .mmp data, 0068FA10 with its three fills 00690970 / 00690B30 / 00690D30, went with
// the DirectDraw textures in R11b: the new renderer decodes the .mmp file, texture_mmp.cpp.)
#include "texture.h"
#include "game_strings.h"

#include "console.h"
#include "text_split.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

namespace {

const char* AlreadyLoadedText() { return game::Text(0x00795BE8); }     // "CTexture::Load.\n Texture \"%s\" already loaded.\n"
const char* AlphaPrefixText() { return game::Text(0x00795BE4); }       // "a_"

} // namespace

// The path is split at `\` (each piece trimmed), the alpha file is "a_" + the file name in the same
// directory; both are joined again.
int CTexture::LoadBmp(CTextureDevice* device, const char* path) {
    mfc::CString file;
    mfc::CString alphaPath;
    console::CStringArray pieces;
    if ((flags_ & kTextureLoaded) &&
        gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(name_), reinterpret_cast<const unsigned char*>(path)) == 0) {
        game::DebugLog(AlreadyLoadedText(), path);
        return 1;
    }
    text::Split(path, &pieces, '\\');
    file = pieces[pieces.GetSize() - 1];
    pieces.RemoveAt(pieces.GetSize() - 1, 1);
    alphaPath = mfc::operator+(AlphaPrefixText(), file);
    pieces.SetAtGrow(pieces.GetSize(), alphaPath);
    text::Join(&alphaPath, &pieces, '\\');
    pieces.RemoveAt(pieces.GetSize() - 1, 1);
    pieces.SetAtGrow(pieces.GetSize(), file);
    text::Join(&file, &pieces, '\\');
    return LoadBmpFiles(device, file, alphaPath);
}
EI_REPLACE_METHOD(0x00690260, CTexture::LoadBmp);
