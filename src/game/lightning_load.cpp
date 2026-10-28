// The lightning noise table's load. Rebirth patches the function (it redirects the
// file lookup to a mod folder's copy): this is the vanilla function and it
// installs in the vanilla launch only (rule 9). The lightning manager's Create calls it through
// its address (LoadLightningNoiseOriginal). Main thread. See docs/notes/objects.md.
#include "lightning.h"

#include "lib/crt.h"
#include "resource_local.h"
#include "world_data.h"

#include <cstdint>
#include <string>


// Opens <text folder>gfx.res and reads lightning.dat into the noise table, row by row through a memory stream
// (a short file leaves the rest zero-filled, as the stream's Read does). Returns 0 when the archive does not
// open (no message), the fatal error's result when the file is missing, else 1.
int LoadLightningNoise() {
    LocalResource resource;
    CMemoryStream local;
    bool opened;
    {
        std::string folder = *g_textFolder;
        std::string path = folder + "gfx.res";
        // Through the vtable.
        opened = resource.Get()->OpenResource(path, 0) != 0;
    }
    if (!opened) return 0;

    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("lightning.dat", &size);
    if (!data) return game::FatalError("Unable to open file gfx.res\\lightning.dat");

    FillStream(local, data, size);
    CStream* stream = &local;
    float* row = g_lightningNoise[0];
    for (int i = 0; i < kLightningNoiseRows; ++i, row += kLightningNoiseRowLength)
        stream->Read(row, kLightningNoiseRowLength * sizeof(float));
    resource.Get()->CMappedResource::CloseFile(data);
    return 1;
}
