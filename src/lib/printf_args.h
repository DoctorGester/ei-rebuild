#pragma once

// printf-style functions (str::Format, game::FatalError, game::DebugLog, SayFormat, ei::Log) take a std::string
// where the format has a %s: the caller passes the string, the call hands its text on. Each such function has a
// template overload next to its C-variadic one; the template takes part only when an argument is a std::string,
// turns every std::string into its c_str() and calls the variadic one.

#include <string>
#include <type_traits>
#include <utility>

namespace libprintf {

template <class T>
inline constexpr bool IsString = std::is_same_v<std::remove_cvref_t<T>, std::string>;

// True when one of the arguments is a std::string.
template <class... Args>
inline constexpr bool HasString = (IsString<Args> || ...);

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
