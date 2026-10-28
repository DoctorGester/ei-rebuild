// Links a figure's pieces from the skeleton file of the model `name`. With the model cached, its mesh archive
// reads the links (GetFile, freed by CloseFile); without it the manager opens "<name>.lnk" (its GetFile /
// CloseFile); with neither the fatal error "Can't load skeleton file" is returned. The data is a count and
// that many (child, parent) length-prefixed names (read into CStrings), each passed to the animation state's
// LinkPiece with the model; FindRoot finishes it. Returns 1. See docs/notes/models.md.
#include "figure_manager.h"       // CFigureManager, FigureModel
#include "map_object_figures.h"   // CFigureAnimationState, FigureBone

#include "lib/crt.h"
#include <cstdint>
#include <string>

#include <cstring>
#include "model.h"

namespace {

// The manager's model of a name.
// The animation state: LinkPiece, FindRoot.
// Game::FatalError(format, ...).


} // namespace

int CFigureManager::LoadSkeletonLinks(CFigureAnimationState* state, std::string_view name) {
    FigureModel* model;
    {
        std::string key(name);
        model = FindModel(&key);
    }

    std::uint32_t* data;
    if (model) {
        data = static_cast<std::uint32_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        std::string base(name);
        std::string path = base + ".lnk";
        data = static_cast<std::uint32_t*>(GetFile(path, nullptr));
    }
    if (!data) return game::FatalError("Can't load skeleton file for \"%s\"\n", std::string(name).c_str());

    const std::uint8_t* cursor = reinterpret_cast<const std::uint8_t*>(data + 1);
    {
        std::string child, parent;
        for (std::uint32_t i = data[0]; i != 0; --i) {
            ReadPrefixedName(&child, &cursor);
            ReadPrefixedName(&parent, &cursor);
            state->LinkPiece(model, child, parent);
        }
        state->FindRoot();
        if (model) model->mesh->CloseFile(data);
        else CloseFile(data);
    }
    return 1;
}

// A bone's cage. The data comes from the cached model's bones archive (GetFile with the bone's name and the
// size out) or from the file "<bone's model name><bone's name>.bon" (the manager's GetFile with the size
// out); none returns 0. 0x48 bytes go to one bone loader, other sizes to another, as {data, size}. The data
// is not released here. Returns 1.
int CFigureManager::LoadFigureBones(FigureModel* model, FigureBone* bone) {
    struct {
        void* data;
        std::uint32_t size;
    } bones{nullptr, 0};
    if (model) {
        bones.data = model->bones->GetFile(bone->name, &bones.size);
    } else {
        std::string name = bone->GetName();
        std::string directory = bone->name3;
        std::string path = (directory + name) + ".bon";
        bones.data = GetFile(path, &bones.size);
    }
    if (!bones.data) return 0;
    // The bone's cage interpolation: 0x48-byte data (a prism of six points), or
    // any other size (a cube of eight); the stream is the data pointer, advanced as it is read.
    auto** stream = reinterpret_cast<const float**>(static_cast<void*>(&bones.data));
    if (bones.size == 0x48) bone->InterpolatePrism(stream);
    else bone->InterpolateCube(stream);
    return 1;
}
