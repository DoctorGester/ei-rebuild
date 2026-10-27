// UnitDatabase::CountSoundFiles: how many sound files each unit model's sound names have, counted once the
// sound system is up (client world creation, the resource switch and the database rebuild). The client
// picks a random file of the counted ones (ActionSound, unit_client_sounds.cpp). See unit_db.h.
#include "sound.h"
#include "unit_db.h"

#include "world.h"

#include "lib/stlp.h"

#include "ei/hook.h"

#include <cstring>

namespace {
constexpr const char* kSoundPattern = "*.wav";

// STLport vector<int>::resize(count, value) (inlined here): fewer elements are dropped, more are appended;
// a full vector grows to size + max(size, added).
void ResizeCounts(UnitModelRecord* model, std::size_t count, std::uint32_t value) {
    stlp::vector<std::uint32_t>& counts = model->soundCounts;
    std::uint32_t*& begin = counts.Start();
    std::uint32_t*& end = counts.Finish();
    std::uint32_t*& endOfStorage = counts.EndOfStorage();
    std::size_t size = static_cast<std::size_t>(end - begin);
    if (count < size) {
        end = begin + count;
        return;
    }
    std::size_t added = count - size;
    if (added == 0) return;
    if (static_cast<std::size_t>(endOfStorage - end) >= added) {
        for (std::size_t i = 0; i < added; ++i) end[i] = value;
        end += added;
        return;
    }
    std::size_t length = size + (size > added ? size : added);
    auto* fresh = stlp::allocate_as<std::uint32_t>(length);
    if (size) std::memcpy(fresh, begin, size * sizeof(std::uint32_t));
    for (std::size_t i = 0; i < added; ++i) fresh[size + i] = value;
    if (begin) stlp::deallocate(begin, static_cast<std::size_t>(endOfStorage - begin) * sizeof(std::uint32_t));
    begin = fresh;
    end = fresh + count;
    endOfStorage = fresh + length;
}

// The three-way compare of the file name cut to the sound name's length with the sound name is 0: the file
// name starts with the sound name.
bool StartsWith(const char* file, const char* name) {
    std::size_t fileLength = std::strlen(file);
    std::size_t nameLength = std::strlen(name);
    return fileLength >= nameLength && std::memcmp(file, name, nameLength) == 0;
}
} // namespace

void UnitDatabase::CountSoundFiles(void* sounds) {
    mfc::CStringArray files;
    files.Construct();
    static_cast<CSoundManager*>(sounds)->ListWaveFiles(&files, kSoundPattern);
    for (int i = 0; i < files.size; ++i) files.data[i].MakeLower();

    int modelCount = static_cast<int>(models.end - models.begin);
    for (int m = 0; m < modelCount; ++m) {
        UnitModelRecord* model = &models.begin[m];
        std::size_t names = static_cast<std::size_t>(model->soundNames.size());
        ResizeCounts(model, names, 0);
        for (std::size_t n = 0; n < names; ++n) {
            model->soundCounts[n] = 0;
            mfc::CString& name = model->soundNames.begin()[n];
            name.MakeLower();
            for (int f = 0; f < files.size; ++f) {
                if (StartsWith(files.data[f], name)) ++model->soundCounts[n];
            }
        }
    }
    files.Destruct();
}

EI_REWRITTEN(0x0053A8E0, UnitDatabase::CountSoundFiles);
