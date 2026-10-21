// UnitDatabase::CountSoundFiles (0053A8E0): how many sound files each unit model's sound names
// have, counted once the sound system is up (client world creation 00563710, 0057CB50, and the
// database rebuild 0057DEE0). The client picks a random file of the counted ones (ActionSound,
// unit_client_sounds.cpp). See unit_db.h.
#include "unit_db.h"

#include "world.h"

#include "lib/stlp.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_units.h"

#include <cstring>

namespace {
constexpr const char* kSoundPattern = "*.wav";   // 0078FCA8

// STLport vector<int>::resize(count, value) (004FE0C0, inlined here): fewer elements are dropped,
// more are appended; a full vector grows to size + max(size, added).
void ResizeCounts(UnitModelRecord* model, std::size_t count, std::uint32_t value) {
    std::size_t size = static_cast<std::size_t>(model->soundCountsEnd - model->soundCounts);
    if (count < size) {
        model->soundCountsEnd = model->soundCounts + count;
        return;
    }
    std::size_t added = count - size;
    if (added == 0) return;
    if (static_cast<std::size_t>(model->soundCountsEndOfStorage - model->soundCountsEnd) >= added) {
        for (std::size_t i = 0; i < added; ++i) model->soundCountsEnd[i] = value;
        model->soundCountsEnd += added;
        return;
    }
    std::size_t length = size + (size > added ? size : added);
    auto* fresh = static_cast<std::uint32_t*>(stlp::allocate(length * sizeof(std::uint32_t)));
    if (size) std::memcpy(fresh, model->soundCounts, size * sizeof(std::uint32_t));
    for (std::size_t i = 0; i < added; ++i) fresh[size + i] = value;
    if (model->soundCounts)
        stlp::deallocate(model->soundCounts,
                         static_cast<std::size_t>(model->soundCountsEndOfStorage - model->soundCounts) * sizeof(std::uint32_t));
    model->soundCounts = fresh;
    model->soundCountsEnd = fresh + count;
    model->soundCountsEndOfStorage = fresh + length;
}

// The original's three-way compare (00445810) of the file name cut to the sound name's length
// with the sound name is 0: the file name starts with the sound name.
bool StartsWith(const char* file, const char* name) {
    std::size_t fileLength = std::strlen(file);
    std::size_t nameLength = std::strlen(name);
    return fileLength >= nameLength && std::memcmp(file, name, nameLength) == 0;
}
} // namespace

void UnitDatabase::CountSoundFiles(void* sounds) {
    mfc::CStringArray files;
    files.Construct();
    SoundSystemListFilesOriginal(sounds, nullptr, &files, kSoundPattern);
    for (int i = 0; i < files.size; ++i) reinterpret_cast<mfc::CString*>(files.data)[i].MakeLower();

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
