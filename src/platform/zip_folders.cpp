// A .zip file read as a folder (zip_folders.h; docs/notes/mods.md, "Zip mods"), on miniz's reader (third_party/miniz).
#include "platform/zip_folders.h"

#include "lib/text_case.h"
#include "platform/path_layers.h"
#include "platform/seams.h"

#include "miniz/miniz.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <system_error>

namespace platform {

namespace {

namespace fs = std::filesystem;

std::string Lower(std::string_view text) {
    std::string lower(text);
    text::LowerInPlace(lower.data(), lower.size());
    return lower;
}

// One zip: its files and folders by their paths below it (lower case, backslashes; "" the root), read through
// miniz from the system's file, one unpacking at a time.
class Zip {
public:
    struct Item {
        std::string name;                 // the last part, as the zip writes it
        bool folder = false;
        std::uint32_t size = 0;
        std::uint32_t crc = 0;            // a file's CRC-32, as the zip's list of files gives it
        mz_uint index = 0;                // a file's entry
        std::vector<std::string> children;   // a folder's items' keys, by name
    };

    explicit Zip(const fs::path& file) : stream(file, std::ios::binary) {
        std::error_code error;
        const std::uintmax_t length = fs::file_size(file, error);
        if (!stream || error) return;
        archive.m_pRead = &Zip::ReadAt;
        archive.m_pIO_opaque = this;
        open = mz_zip_reader_init(&archive, length, 0) != MZ_FALSE;
        if (!open) return;
        items[""].folder = true;
        const mz_uint count = mz_zip_reader_get_num_files(&archive);
        unpacked.resize(count);
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&archive, i, &stat) || stat.m_uncomp_size > UINT32_MAX) continue;
            std::string path = stat.m_filename;
            std::replace(path.begin(), path.end(), '/', '\\');
            while (!path.empty() && path.back() == '\\') path.pop_back();
            if (!path.empty())
                Add(path, stat.m_is_directory != 0, static_cast<std::uint32_t>(stat.m_uncomp_size), stat.m_crc32, i);
        }
        for (auto& [key, item] : items)
            std::sort(item.children.begin(), item.children.end(), [this](const std::string& a, const std::string& b) {
                return text::CompareIgnoringCase(items.at(a).name, items.at(b).name) < 0;
            });
    }
    ~Zip() {
        if (open) mz_zip_reader_end(&archive);
    }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    bool IsOpen() const { return open; }

    const Item* Find(std::string_view path) const {
        const auto found = items.find(Lower(path));
        return found == items.end() ? nullptr : &found->second;
    }

    // A file's bytes, unpacked once while any holder keeps them.
    std::shared_ptr<const std::string> Bytes(const Item& item) {
        std::lock_guard<std::mutex> hold(lock);
        std::shared_ptr<const std::string> bytes = unpacked[item.index].lock();
        if (bytes) return bytes;
        auto made = std::make_shared<std::string>(item.size, '\0');
        if (!mz_zip_reader_extract_to_mem(&archive, item.index, made->data(), made->size(), 0)) return nullptr;
        unpacked[item.index] = made;
        return made;
    }

private:
    // `path` and the folders above it; a path met before (in any case) keeps its first entry.
    void Add(const std::string& path, bool folder, std::uint32_t size, std::uint32_t crc, mz_uint index) {
        const std::string key = Lower(path);
        const auto [at, made] = items.try_emplace(key);
        if (!made) return;
        Item& item = at->second;
        const std::size_t cut = path.find_last_of('\\');
        item.name = path.substr(cut == std::string::npos ? 0 : cut + 1);
        item.folder = folder;
        item.size = folder ? 0 : size;
        item.crc = folder ? 0 : crc;
        item.index = index;
        const std::string parent = cut == std::string::npos ? std::string() : key.substr(0, cut);
        if (items.count(parent) == 0) Add(path.substr(0, cut), true, 0, 0, 0);
        items[parent].children.push_back(key);
    }

    static size_t ReadAt(void* opaque, mz_uint64 offset, void* buffer, size_t count) {
        Zip& zip = *static_cast<Zip*>(opaque);
        zip.stream.clear();
        zip.stream.seekg(static_cast<std::streamoff>(offset));
        zip.stream.read(static_cast<char*>(buffer), static_cast<std::streamsize>(count));
        return static_cast<size_t>(zip.stream.gcount());
    }

    std::ifstream stream;
    mz_zip_archive archive{};
    bool open = false;
    std::map<std::string, Item> items;
    std::vector<std::weak_ptr<const std::string>> unpacked;   // by entry
    std::mutex lock;   // miniz's reader and the stream: one unpacking at a time (the game's threads read files too)
};

// The zips read so far, by their full paths in lower case; never closed (other threads may hold their files).
std::mutex g_zipsLock;
std::map<std::string, std::unique_ptr<Zip>>& Zips() {
    static auto& zips = *new std::map<std::string, std::unique_ptr<Zip>>;
    return zips;
}

// Whether the path `part` ends in ".zip".
bool NamesZip(std::string_view part) {
    constexpr std::string_view kZip = ".zip";
    return part.size() > kZip.size() && text::CompareIgnoringCase(part.substr(part.size() - kZip.size()), kZip) == 0;
}

// The zip `path` goes through and the rest of the path below it: the first part of the path ending in ".zip" that is
// a zip file. Null for any other path.
Zip* ZipOf(std::string_view path, std::string* rest) {
    // Most paths have no part ending in ".zip": no system call for them.
    bool named = false;
    for (std::size_t end = 0; !named && end <= path.size(); ++end)
        named = (end == path.size() || path[end] == '\\' || path[end] == '/') && NamesZip(path.substr(0, end));
    if (!named) return nullptr;
    const std::string full = FullGamePath(path);
    for (std::size_t stop = full.find('\\');; stop = full.find('\\', stop + 1)) {
        const std::string_view part(full.data(), stop == std::string::npos ? full.size() : stop);
        if (NamesZip(part)) {
            const std::string key = Lower(part);
            std::lock_guard<std::mutex> hold(g_zipsLock);
            auto found = Zips().find(key);
            if (found == Zips().end()) {
                const fs::path file = NativePath(part);
                std::error_code error;
                if (fs::is_regular_file(file, error)) {
                    auto zip = std::make_unique<Zip>(file);
                    if (zip->IsOpen()) found = Zips().emplace(key, std::move(zip)).first;
                }
            }
            if (found != Zips().end()) {
                *rest = part.size() < full.size() ? full.substr(part.size() + 1) : std::string();
                return found->second.get();
            }
        }
        if (stop == std::string::npos) return nullptr;
    }
}

} // namespace

bool InZip(std::string_view path, ZipPlace* place) {
    std::string rest;
    Zip* zip = ZipOf(path, &rest);
    if (!zip) return false;
    if (place) {
        *place = {};
        if (const Zip::Item* item = zip->Find(rest)) *place = {true, item->folder, item->size, item->crc};
    }
    return true;
}

bool ZipFile(std::string_view path, std::shared_ptr<const std::string>* bytes) {
    std::string rest;
    Zip* zip = ZipOf(path, &rest);
    if (!zip) return false;
    const Zip::Item* item = zip->Find(rest);
    *bytes = item && !item->folder ? zip->Bytes(*item) : nullptr;
    return true;
}

bool SearchZip(std::string_view pattern, const std::string& prefix, bool dots, std::vector<FolderEntry>* entries) {
    const std::size_t slash = pattern.find_last_of("\\/");
    if (slash == std::string_view::npos) return false;
    std::string rest;
    Zip* zip = ZipOf(pattern.substr(0, slash), &rest);
    if (!zip) return false;
    const std::string_view mask = pattern.substr(slash + 1);
    const Zip::Item* folder = zip->Find(rest);
    if (!folder || !folder->folder) return true;
    if (dots)
        for (const char* name : {".", ".."})
            if (WildcardMatches(mask, name)) entries->push_back({name, prefix + name, 0, true});
    for (const std::string& key : folder->children) {
        const Zip::Item& item = *zip->Find(key);
        if (WildcardMatches(mask, item.name)) entries->push_back({item.name, prefix + item.name, item.size, item.folder});
    }
    return true;
}

bool CopySystemFile(const std::string& from, const std::string& to, bool replace) {
    std::shared_ptr<const std::string> bytes;
    std::error_code error;
    if (!ZipFile(from, &bytes))
        return fs::copy_file(NativePath(from), NativePath(to),
                             replace ? fs::copy_options::overwrite_existing : fs::copy_options::none, error);
    if (!bytes || (!replace && fs::exists(NativePath(to), error))) return false;
    std::ofstream file(NativePath(to), std::ios::binary | std::ios::trunc);
    file.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    return static_cast<bool>(file);
}

bool WildcardMatches(std::string_view mask, std::string_view name) {
    if (mask == "*.*") mask = "*";
    std::size_t m = 0, n = 0, star = mask.npos, retry = 0;
    while (n < name.size()) {
        if (m < mask.size() && (mask[m] == '?' || text::ToLower(mask[m]) == text::ToLower(name[n]))) {
            ++m;
            ++n;
        } else if (m < mask.size() && mask[m] == '*') {
            star = m++;
            retry = n;
        } else if (star != mask.npos) {
            m = star + 1;
            n = ++retry;
        } else return false;
    }
    while (m < mask.size() && mask[m] == '*') ++m;
    return m == mask.size();
}

unsigned ReadZipped(const std::string& bytes, std::uint32_t* position, void* buffer, unsigned count) {
    const std::uint32_t size = static_cast<std::uint32_t>(bytes.size());
    const unsigned read = *position >= size ? 0 : std::min<unsigned>(count, size - *position);
    if (read) std::memcpy(buffer, bytes.data() + *position, read);
    *position += read;
    return read;
}

bool SeekZipped(std::uint32_t size, std::uint32_t* position, std::int32_t offset, SeekFrom from) {
    const std::int64_t base = from == SeekFrom::Begin ? 0 : from == SeekFrom::Current ? *position : size;
    const std::int64_t to = base + offset;
    if (to < 0 || to > UINT32_MAX) return false;
    *position = static_cast<std::uint32_t>(to);
    return true;
}

} // namespace platform
