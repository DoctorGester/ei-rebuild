// Loading a .bmp texture with its alpha file. (The fill of a texture's system-memory
// surface from .mmp data went with
// the DirectDraw textures: the new renderer decodes the .mmp file, texture_mmp.cpp.)
#include "texture.h"
#include "game_strings.h"

#include "console.h"
#include "text_split.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

namespace {

const char* AlreadyLoadedText() { return "CTexture::Load.\n Texture \"%s\" already loaded.\n"; }     // "CTexture::Load.\n Texture \"%s\" already loaded.\n"
const char* AlphaPrefixText() { return "a_"; }

} // namespace

// The path is split at `\` (each piece trimmed), the alpha file is "a_" + the file name in the same
// directory; both are joined again.
int CTexture::LoadBmp(CTextureDevice* device, const char* path) {
    std::string file;
    std::string alphaPath;
    std::vector<std::string> pieces;
    if ((flags_ & kTextureLoaded) &&
        gamecrt::mbscmp(name_, path) == 0) {
        game::DebugLog(AlreadyLoadedText(), path);
        return 1;
    }
    text::Split(path, &pieces, '\\');
    file = pieces.back();
    pieces.pop_back();
    alphaPath = AlphaPrefixText() + file;
    pieces.push_back(alphaPath);
    text::Join(&alphaPath, pieces, '\\');
    pieces.pop_back();
    pieces.push_back(file);
    text::Join(&file, pieces, '\\');
    return LoadBmpFiles(device, file.c_str(), alphaPath.c_str());
}
EI_REWRITTEN(0x00690260, CTexture::LoadBmp);
