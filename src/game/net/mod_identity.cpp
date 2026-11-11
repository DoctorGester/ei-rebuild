// The mod a game plays as our peers tell it (mod_identity.h).
#include "game/net/mod_identity.h"

#include "game/app/singletons.h"
#include "game/data/registry.h"
#include "game/data/text_manager.h"
#include "game/mods/mod_hash.h"
#include "game/ui/ui.h"
#include "game/world/world.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <chrono>
#include <future>
#include <initializer_list>

namespace modidentity {
namespace {

// The mod folder Played's identity was made for (made: once made, also for no mod).
struct PlayedMod {
    bool made = false;
    std::string folder;
    Identity identity;
};
PlayedMod g_played;

// A content hash being made on a thread of its own (Prepare): the folder's, and the time it took.
struct Hashed {
    std::uint64_t hash = 0;
    modhash::Count count = {};
    double seconds = 0.0;
};
struct PendingHash {
    std::string folder;
    std::future<Hashed> result = {};
};
PendingHash g_pending;

Hashed HashOf(const std::string& folder) {
    const auto start = std::chrono::steady_clock::now();
    Hashed hashed;
    hashed.hash = modhash::ContentHash(folder, &hashed.count);
    hashed.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return hashed;
}

// Our texts, which the game's texts lack: Russian, English.
struct AddedText {
    const char* key = nullptr;
    const char* russian = nullptr;
    const char* english = nullptr;
};
constexpr AddedText kTitle = {"lmp_mod_mismatch", "Другой мод", "Another mod"};
constexpr AddedText kMismatch = {
    "lmp_mod_mismatch_msg", "Ведущий играет: %s. Эта игра: %s. Чтобы войти, выберите тот же мод в главном меню.",
    "The host plays: %s. This game plays: %s. Choose the same mod in the main menu to join."};
constexpr AddedText kVersion = {
    "lmp_mod_version_msg", "Ведущий играет другую версию мода %s: файлы мода отличаются от файлов этой игры.",
    "The host plays another version of %s: the mod's files differ from this game's."};
constexpr AddedText kOriginal = {
    "lmp_mod_original_msg", "Ведущий играет в оригинальную игру, а она не может играть мод %s. Войдите без мода.",
    "The host runs the original game, which cannot play %s. Join without the mod."};
constexpr AddedText kRefused = {"lmp_mod_refused", "Игрок с адреса %s не принят: его мод: %s, мод этой игры: %s.",
                                "A join from %s was refused for its mod: it plays %s, this game %s."};
constexpr AddedText kNoMod = {"lmp_no_mod", "без мода", "no mod"};
constexpr AddedText kOriginalGame = {"lmp_original_game", "оригинальная игра", "the original game"};
constexpr AddedText kOtherFiles = {"lmp_mod_other_files", "%s (другие файлы)", "%s (other files)"};
constexpr AddedText kListed = {"lmp_mod_listed", "Мод: %s", "Mod: %s"};
constexpr AddedText kCannotJoin = {"lmp_mod_cannot_join", "Не войти: моды разные", "Cannot join: the mods differ"};
constexpr AddedText kCannotJoinVersion = {"lmp_mod_cannot_join_version", "Не войти: другая версия мода",
                                          "Cannot join: another version of the mod"};

// The game's text of the key when it has one, else ours.
std::string Text(const AddedText& text) {
    if (TextManager* texts = g_clientWorld.texts.get())
        if (std::optional<std::string> found = texts->FindString(text.key)) return *found;
    return GameTextsInRussian() ? text.russian : text.english;
}

// `format` with each "%s" in turn replaced by the next of `values` (a text from a mod's files is no printf format).
std::string Filled(std::string format, std::initializer_list<std::string_view> values) {
    std::size_t at = 0;
    for (const std::string_view value : values) {
        at = format.find("%s", at);
        if (at == std::string::npos) break;
        format.replace(at, 2, value);
        at += value.size();
    }
    return format;
}

// The mod as the texts name it: its title, "no mod" or "the original game".
std::string Shown(const std::optional<Identity>& mod) {
    if (!mod) return Text(kOriginalGame);
    return mod->title.empty() ? Text(kNoMod) : mod->title;
}

// The hash in 16 hexadecimal digits.
std::string Hex(std::uint64_t hash) {
    std::string digits(16, '0');
    for (int i = 15; i >= 0; --i, hash >>= 4) digits[i] = "0123456789abcdef"[hash & 0xF];
    return digits;
}

} // namespace

void Prepare() {
    const std::string folder = platform::ModFolder();
    if (folder.empty() || (g_played.made && g_played.folder == folder)) return;
    if (g_pending.result.valid() && g_pending.folder == folder) return;
    if (g_pending.result.valid()) g_pending.result.wait();   // another mod's, no longer wanted
    g_pending = {folder, std::async(std::launch::async, HashOf, folder)};
}

const Identity& Played() {
    const std::string folder = platform::ModFolder();
    if (g_played.made && g_played.folder == folder) return g_played.identity;
    g_played = {true, folder, {}};
    if (folder.empty()) return g_played.identity;
    Identity& identity = g_played.identity;
    FileRegistry registry;
    if (registry.Open(folder + "\\config.reg")) identity.title = registry.GetString("MOD", "Title", "");
    if (identity.title.empty()) identity.title = platform::ModName();
    const auto start = std::chrono::steady_clock::now();
    Hashed hashed;
    if (g_pending.result.valid() && g_pending.folder == folder) hashed = g_pending.result.get();
    else hashed = HashOf(folder);
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    identity.hash = hashed.hash;
    ei::Log("mod: \"%s\", content hash %s over %u files of %.1f MB (made in %.2f s, waited %.2f s)", identity.title,
            Hex(identity.hash), hashed.count.files, static_cast<double>(hashed.count.bytes) / (1024.0 * 1024.0),
            hashed.seconds, waited);
    return identity;
}

bool Agrees(const std::optional<Identity>& theirs) { return theirs.value_or(Identity{}) == Played(); }

Box RefusalBox(const std::optional<Identity>& host) {
    const Identity& ours = Played();
    Box box{Text(kTitle), {}};
    if (!host && !ours.title.empty()) box.text = Filled(Text(kOriginal), {ours.title});
    else if (host && !host->title.empty() && host->title == ours.title) box.text = Filled(Text(kVersion), {ours.title});
    else box.text = Filled(Text(kMismatch), {Shown(host), Shown(ours)});
    return box;
}

std::string RefusedLine(std::string_view address, const std::optional<Identity>& theirs) {
    const Identity& ours = Played();
    std::string shown = Shown(theirs);
    if (theirs && !theirs->title.empty() && theirs->title == ours.title) shown = Filled(Text(kOtherFiles), {shown});
    return Filled(Text(kRefused), {address, shown, Shown(ours)});
}

Listing Listed(const std::optional<Identity>& host) {
    Listing listing{Filled(Text(kListed), {Shown(host)}), {}};
    if (!Agrees(host)) {
        const bool version = host && !host->title.empty() && host->title == Played().title;
        listing.cannotJoin = Text(version ? kCannotJoinVersion : kCannotJoin);
    }
    return listing;
}

std::string Described(const std::optional<Identity>& mod) {
    if (!mod) return "no mark (the original game)";
    if (mod->title.empty()) return "no mod";
    return "\"" + mod->title + "\" " + Hex(mod->hash);
}

} // namespace modidentity
