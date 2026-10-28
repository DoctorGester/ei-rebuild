// The camera's recorded path file load: the file open is redirected to a mod folder's copy
// (CameraPatch, mod_folders.h). Its callers (the camera commands' glue) reach it through its address. Main thread.
// See docs/notes/core.md.
#include "camera.h"

#include "mod_folders.h"
#include "world_data.h"

#include "lib/file_io.h"
#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"

#include <string>

namespace {
// The gap in the keys' time that ends the lead-in.
constexpr float kLeadInGap = 500.0f;
} // namespace

int CCamera::LoadPath(const char* name, int settle) {
    lib::File file;
    int opened;
    {
        std::string folder = TheDataFolder();
        std::string path = folder + "camera\\" + name;
        if (modfolders::Active(ei::rebirth::Patch::CameraPatch)) {
            // CameraPatch: the mod's Camera\ copy when there is one.
            opened = file.Open(modfolders::CameraFile(path.c_str()), lib::File::modeRead);
        } else {
            opened = file.Open(path, lib::File::modeRead);
        }
    }
    if (!opened) return 0;

    // As many keys as the file holds whole; the array is resized as the engine's aligned arrays
    // are (the old keys kept when it grows) and filled with one read.
    keys_.resize(static_cast<int>(file.GetLength() / sizeof(CameraKey)));
    file.Read(keys_.data(), static_cast<unsigned>(std::ssize(keys_)) * sizeof(CameraKey));

    if (settle != 0 && std::ssize(keys_) > settle && settle > 1) {
        // The first key (of the first `settle`) more than the gap after its predecessor: every key
        // before it takes its position and rotation (the times stay).
        for (int first = 1; first < settle; ++first) {
            EI_NAN_AUDIT(keys_[first].time, keys_[first - 1].time);
            if (keys_[first].time - keys_[first - 1].time <= kLeadInGap) continue;
            for (int i = 0; i < first; ++i) {
                keys_[i].position = keys_[first].position;
                keys_[i].rotation = keys_[first].rotation;
            }
            break;
        }
    }
    file.Close();
    return 1;
}
