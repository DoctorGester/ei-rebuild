// UnitDatabase::CountSoundFiles: how many sound files each unit model's sound names have, counted once the
// sound system is up (client world creation, the resource switch and the database rebuild). The client
// picks a random file of the counted ones (ActionSound, unit_client_sounds.cpp). See unit_db.h.
#include "sound.h"
#include "unit_db.h"

#include "world.h"

#include "lib/stlp.h"
#include "lib/str.h"

#include <cstring>
#include <vector>

namespace {
constexpr const char* kSoundPattern = "*.wav";

// The three-way compare of the file name cut to the sound name's length with the sound name is 0: the file
// name starts with the sound name.
bool StartsWith(const char* file, const char* name) {
    std::size_t fileLength = std::strlen(file);
    std::size_t nameLength = std::strlen(name);
    return fileLength >= nameLength && std::memcmp(file, name, nameLength) == 0;
}
} // namespace

void UnitDatabase::CountSoundFiles(void* sounds) {
    std::vector<std::string> files;
    static_cast<CSoundManager*>(sounds)->ListWaveFiles(&files, kSoundPattern);
    for (std::string& file : files) str::MakeLower(file);

    int modelCount = static_cast<int>(models.end - models.begin);
    for (int m = 0; m < modelCount; ++m) {
        UnitModelRecord* model = &models.begin[m];
        std::size_t names = static_cast<std::size_t>(model->soundNames.size());
        model->soundCounts.resize(names, 0);
        for (std::size_t n = 0; n < names; ++n) {
            model->soundCounts[n] = 0;
            std::string& name = model->soundNames.begin()[n];
            str::MakeLower(name);
            for (const std::string& file : files) {
                if (StartsWith(file.c_str(), name.c_str())) ++model->soundCounts[n];
            }
        }
    }
}
