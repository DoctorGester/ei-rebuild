// The lightning noise table's load (00468A00; A-E1). Rebirth patches the function (it redirects the
// file lookup, the call at 00468B16, to a mod folder's copy): this is the vanilla function and it
// installs in the vanilla launch only (rule 9). The lightning manager's Create calls it through
// its address (LoadLightningNoiseOriginal). Main thread. See docs/notes/objects.md.
#include "lightning.h"

#include "lib/crt.h"
#include "resource_local.h"
#include "world_data.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>
#include <string>

// 00468A00 (cdecl, no arguments). Opens <text folder>gfx.res and reads lightning.dat into the noise
// table, row by row through a memory stream (a short file leaves the rest zero-filled, as the
// stream's Read does). Returns 0 when the archive does not open (no message), the fatal error's
// result when the file is missing, else 1.
int __cdecl LoadLightningNoise() {
    LocalResource resource;
    CMemoryStream local;
    bool opened;
    {
        std::string folder = TheTextFolder().GetString();
        std::string path = folder + "gfx.res";
        // Through the vtable: slot 1 is 00671730.
        opened = resource.Get()->OpenResource(path.c_str(), 0) != 0;
    }
    if (!opened) return 0;

    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("lightning.dat", &size);
    if (!data) return game::FatalError("Unable to open file gfx.res\\lightning.dat");

    FillStream(local, data, size);
    CStream* stream = &local;
    float* row = LightningNoiseTable();
    for (int i = 0; i < kLightningNoiseRows; ++i, row += kLightningNoiseRowLength)
        stream->Read(row, kLightningNoiseRowLength * sizeof(float));
    resource.Get()->CMappedResource::CloseFile(data);
    return 1;
}
EI_REWRITTEN(0x00468A00, LoadLightningNoise);
