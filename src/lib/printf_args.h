#pragma once

// printf-style functions (str::Format, game::FatalExit, game::Warning, game::DebugLog, SayFormat, ei::Log, ...) take a
// std::string or std::string_view where the format has a %s: the caller passes the string, the call hands its text on.
// Each such function has a template overload next to its C-variadic one; the template takes part when an argument is
// a class object, turns every std::string into its c_str() (a std::string_view into a copy's, alive until the end of
// the call) and calls the variadic one. Any other class object is a compile error: through "..." it would be logged
// as garbage.

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// Marks a printf-style function: its format is parameter `formatIndex` (from 1), its arguments start at
// `firstArg`. gcc and clang check each call's arguments against a literal format (-Wformat); MSVC checks formats
// only under /analyze.
#if defined(__GNUC__)
#define EI_PRINTF_FORMAT(formatIndex, firstArg) [[gnu::format(printf, formatIndex, firstArg)]]
#else
#define EI_PRINTF_FORMAT(formatIndex, firstArg)
#endif

namespace libprintf {

template <class T>
inline constexpr bool IsString = std::is_same_v<std::remove_cvref_t<T>, std::string>;

template <class T>
inline constexpr bool IsStringView = std::is_same_v<std::remove_cvref_t<T>, std::string_view>;

// True when one of the arguments is a class object (the template overloads take it, Arg checks it).
template <class... Args>
inline constexpr bool HasClass = (std::is_class_v<std::remove_cvref_t<Args>> || ...);

// A std::string_view is not zero-terminated: its text as a std::string, which Arg then hands on as c_str(). The copy
// lives until the end of the full expression that calls the variadic function. Anything else as it is.
template <class T>
decltype(auto) Owned(T&& value) {
    if constexpr (IsStringView<T>) {
        return std::string(value);
    } else {
        return std::forward<T>(value);
    }
}

// The argument as the variadic function takes it: a string's text, anything else as it is. A class object
// (other than a string) has no place among printf's arguments.
template <class T>
decltype(auto) Arg(T&& value) {
    if constexpr (IsString<T>) {
        return value.c_str();
    } else {
        static_assert(!std::is_class_v<std::remove_cvref_t<T>>, "a class object among printf-style arguments");
        return std::forward<T>(value);
    }
}

} // namespace libprintf
