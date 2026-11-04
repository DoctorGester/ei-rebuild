// The platform's keyboard on Linux and macOS (platform.h, "The keyboard"): SDL's keymap (the system's active layout)
// read as Windows' layouts give their codes, control characters and key names; what a letter is by the C library.
#include "platform/platform.h"

#include "platform/sdl/sdl_keys.h"

#include <SDL3/SDL.h>
#include <cwctype>
#include <string>
#include <locale.h>

namespace platform {

namespace {

struct UsKeyName {
    int scan;
    const char* name;
};

// Windows' US layout's key names (GetKeyNameTextW), by set-1 scan code (0x100 extended). Of the keys a layout moves
// only the punctuation's, which KeyName takes when the active layout's key types no character there.
constexpr UsKeyName kUsKeyNames[] = {
    {0x01, "Esc"},        {0x0C, "-"},           {0x0D, "="},           {0x0E, "Backspace"},   {0x0F, "Tab"},
    {0x1A, "["},          {0x1B, "]"},           {0x1C, "Enter"},       {0x1D, "Ctrl"},        {0x27, ";"},
    {0x28, "'"},          {0x29, "`"},           {0x2A, "Shift"},       {0x2B, "\\"},          {0x33, ","},
    {0x34, "."},          {0x35, "/"},           {0x36, "Right Shift"}, {0x37, "Num *"},       {0x38, "Alt"},
    {0x39, "Space"},      {0x3A, "Caps Lock"},   {0x3B, "F1"},          {0x3C, "F2"},          {0x3D, "F3"},
    {0x3E, "F4"},         {0x3F, "F5"},          {0x40, "F6"},          {0x41, "F7"},          {0x42, "F8"},
    {0x43, "F9"},         {0x44, "F10"},         {0x45, "Pause"},       {0x46, "Scroll Lock"}, {0x47, "Num 7"},
    {0x48, "Num 8"},      {0x49, "Num 9"},       {0x4A, "Num -"},       {0x4B, "Num 4"},       {0x4C, "Num 5"},
    {0x4D, "Num 6"},      {0x4E, "Num +"},       {0x4F, "Num 1"},       {0x50, "Num 2"},       {0x51, "Num 3"},
    {0x52, "Num 0"},      {0x53, "Num Del"},     {0x54, "Sys Req"},     {0x56, "\\"},          {0x57, "F11"},
    {0x58, "F12"},        {0x11C, "Num Enter"},  {0x11D, "Right Ctrl"}, {0x135, "Num /"},      {0x137, "Prnt Scrn"},
    {0x138, "Right Alt"}, {0x145, "Num Lock"},   {0x146, "Break"},      {0x147, "Home"},       {0x148, "Up"},
    {0x149, "Page Up"},   {0x14B, "Left"},       {0x14D, "Right"},      {0x14F, "End"},        {0x150, "Down"},
    {0x151, "Page Down"}, {0x152, "Insert"},     {0x153, "Delete"},     {0x15B, "Left Windows"},
    {0x15C, "Right Windows"}, {0x15D, "Application"},
};

const char* UsName(int scan) {
    for (const UsKeyName& entry : kUsKeyNames)
        if (entry.scan == scan) return entry.name;
    return nullptr;
}

// What the active layout's key with the set-1 scan code `scan` types without modifiers: SDL's keymap as the system
// gave it (Cyrillic letters on a Russian layout, where SDL's key codes for the events are the US layout's).
SDL_Keycode LayoutTyped(int scan) { return SDL_GetKeyFromScancode(sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, false); }

} // namespace

bool IsLetter(std::uint32_t c) {
    static locale_t locale = [] {
        locale_t found = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
        return found ? found : newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
    }();
    return c <= 0xFFFF && (locale ? iswalpha_l(static_cast<wint_t>(c), locale) : std::iswalpha(static_cast<wint_t>(c)));
}

void ActivateKeyboardLayout(std::uint32_t, std::uint32_t) {}   // Text input follows the user's active system layout.

// What the key types as SDL's key codes for the events name it (SDL_HINT_KEYCODE_OPTIONS' defaults): on a layout whose
// letter keys type Latin letters the layout's character (AZERTY's M key VK_M, its comma key VK_OEM_COMMA; a French
// digit row, digits with Shift, its digits), on another (Russian, Greek) the US layout's, as Windows' Russian layout
// keeps the US codes. A key that types none of KeyOfTyped's characters keeps its US code (KeyOf).
Key sdl_keys::LayoutKey(int scan) {
    if (!sdl_keys::LayoutMoves(scan)) return Key::None;
    return sdl_keys::KeyOfTyped(SDL_GetKeyFromScancode(sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, true));
}

// A letter key (its code a letter's) and the keys layouts do not move: sdl_keys' rule (-1). Another key types ESC, FS
// or GS with Ctrl where the layout's key types `[`, `\` or `]` (Windows' US layout's and xkb's rule), else nothing:
// Russian's х and ъ keys none, as on Windows.
int sdl_keys::LayoutControlCharacter(Key key, int scan) {
    if (!sdl_keys::LayoutMoves(scan) || (key >= Key::A && key <= Key::Z)) return -1;
    switch (LayoutTyped(scan)) {
    case '[': return 0x1B;
    case '\\': return 0x1C;
    case ']': return 0x1D;
    default: return 0;
    }
}

// As Windows' GetKeyNameTextW names them: a letter key by its code's letter (Russian's Й key "Q"), another key layouts
// move by what it types (Russian's х key "х", a French digit row its characters), the other keys by Windows' US names
// (the system has no names in the layout's language).
std::string KeyName(int scan) {
    char text[8] = {};
    const char* chosen = UsName(scan);
    if (sdl_keys::LayoutMoves(scan)) {
        const Key key = sdl_keys::KeyOf(sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, sdl_keys::LayoutKey).key;
        const SDL_Keycode typed = LayoutTyped(scan);
        if (key >= Key::A && key <= Key::Z) {
            text[0] = static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(Key::A)));
            chosen = text;
        } else if (typed >= 0x20 && typed != 0x7F && !(typed & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK))) {
            SDL_UCS4ToUTF8(typed, text);
            chosen = text;
        }
    }
    return chosen ? chosen : "";
}

} // namespace platform
