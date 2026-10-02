#include "resource.h"

#include "ei/hook.h"
#include "lib/crt.h"

int CMappedResource::FindEntry(const char* name) const {
    std::uint32_t count = header_.count;
    if (count == 0) return -1;

    std::uint32_t hash = 0;
    std::uint32_t length = 0;
    for (; name[length] != '\0'; ++length)
        hash += gamecrt::tolower(static_cast<unsigned char>(name[length]));

    std::uint32_t index = hash % count;
    for (;;) {
        const ResEntry& e = entries_[index];
        if (length == e.nameLength && gamecrt::strnicmp(name, names_ + e.nameOffset, length) == 0)
            return static_cast<int>(index);
        index = static_cast<std::uint32_t>(entries_[index].next);
        if (index == 0xFFFFFFFF) return -1;
    }
}
EI_REPLACE_METHOD(0x00674850, CMappedResource::FindEntry);
