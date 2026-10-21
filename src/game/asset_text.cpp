// Asset text to UTF-8 and, until U2-U4, back at the ANSI calls. See asset_text.h.
#include "asset_text.h"

#include <algorithm>
#include <cstring>

#include <windows.h>

namespace text {

namespace {

bool IsAscii(std::string_view bytes) {
    for (char c : bytes) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
    return true;
}

// `bytes` in `from` as UTF-16 (empty when `from` rejects them: MB_ERR_INVALID_CHARS for UTF-8).
std::wstring Widen(std::string_view bytes, unsigned from, DWORD flags) {
    const int size = static_cast<int>(bytes.size());
    const int count = MultiByteToWideChar(from, flags, bytes.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0) MultiByteToWideChar(from, flags, bytes.data(), size, wide.data(), count);
    return wide;
}

std::string Narrow(const std::wstring& wide, unsigned to) {
    const int size = static_cast<int>(wide.size());
    const int count = WideCharToMultiByte(to, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) WideCharToMultiByte(to, 0, wide.data(), size, bytes.data(), count, nullptr, nullptr);
    return bytes;
}

} // namespace

std::string FromAssetText(std::string_view bytes, unsigned codepage) {
    if constexpr (!kUtf8) {
        (void)codepage;
        return std::string(bytes);
    } else {
        if (IsAscii(bytes)) return std::string(bytes);
        // Every byte of the single-byte code pages the game's data uses maps to one character and back
        // (1250-1252 checked: all 255 non-zero bytes), so the ANSI boundary restores the asset's bytes.
        return Narrow(Widen(bytes, codepage, 0), CP_UTF8);
    }
}

void FromAssetText(mfc::CString* text, unsigned codepage) {
    if constexpr (kUtf8) {
        const std::string_view bytes(static_cast<const char*>(*text), static_cast<std::size_t>(text->GetLength()));
        if (IsAscii(bytes)) return;
        const std::string converted = FromAssetText(bytes, codepage);
        const int length = static_cast<int>(converted.size());
        std::memcpy(text->GetBufferSetLength(length), converted.data(), converted.size());
        text->ReleaseBuffer(length);
    } else {
        (void)text;
        (void)codepage;
    }
}

bool ToAssetText(std::string_view text, std::string* out, unsigned codepage) {
    if constexpr (kUtf8) {
        if (IsAscii(text)) return false;
        const std::wstring wide = Widen(text, CP_UTF8, MB_ERR_INVALID_CHARS);
        if (wide.empty()) return false;   // not UTF-8: 8-bit text that has not been converted yet
        *out = Narrow(wide, codepage);
        return true;
    } else {
        (void)text;
        (void)out;
        (void)codepage;
        return false;
    }
}

void ToAssetText(mfc::CString* text, unsigned codepage) {
    std::string converted;
    if (!ToAssetText(std::string_view(static_cast<const char*>(*text), static_cast<std::size_t>(text->GetLength())),
                     &converted, codepage))
        return;
    const int length = static_cast<int>(converted.size());
    std::memcpy(text->GetBufferSetLength(length), converted.data(), converted.size());
    text->ReleaseBuffer(length);
}

int CharacterCount(std::string_view text) {
    if constexpr (!kUtf8) {
        return static_cast<int>(text.size());
    } else {
        constexpr std::size_t kEscapeLength = 5;   // "\007C" and the three colour bytes
        int count = 0;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '\007' && i + 1 < text.size() && text[i + 1] == 'C') {
                const std::size_t length = (std::min)(kEscapeLength, text.size() - i);
                count += static_cast<int>(length);
                i += length;
                continue;
            }
            // A UTF-8 sequence counts by its lead byte; the continuation bytes (10xxxxxx) do not count.
            if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++count;
            ++i;
        }
        return count;
    }
}

void ForAnsiCall::Convert() {
    if (!text_) return;
    const std::string_view bytes = length_ < 0 ? std::string_view(text_) : std::string_view(text_, static_cast<std::size_t>(length_));
    if (!ToAssetText(bytes, &converted_)) return;
    text_ = converted_.c_str();
    if (length_ >= 0) length_ = static_cast<int>(converted_.size());
}

} // namespace text
