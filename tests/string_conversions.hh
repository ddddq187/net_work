#ifndef SPONGE_TESTS_STRING_CONVERSIONS_HH
#define SPONGE_TESTS_STRING_CONVERSIONS_HH

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// Include WrappingInt32 if available (for lab1)
// Note: This file may not exist until lab1 code is fully merged
// We use a forward declaration approach to avoid compilation errors
// when the file doesn't exist yet
#ifdef __has_include
#if __has_include("wrapping_integers.hh")
#include "wrapping_integers.hh"
#define HAS_WRAPPING_INTEGERS
#endif
#else
// Fallback: try to include anyway (will fail if not present)
// This is for compilers that don't support __has_include
#include "wrapping_integers.hh"
#define HAS_WRAPPING_INTEGERS
#endif

inline std::string to_string(const std::string &value) { return value; }
inline std::string to_string(std::string_view value) { return std::string{value}; }
inline std::string to_string(const char *value) { return std::string{value}; }
inline std::string to_string(const bool value) { return value ? "true" : "false"; }

template <class T>
inline std::enable_if_t<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, std::string> to_string(
    const T value) {
    return std::to_string(value);
}

template <class T>
inline std::string to_string(const std::optional<T> &opt) {
    if (!opt.has_value()) {
        return "<empty>";
    }
    return to_string(*opt);
}

// Support for WrappingInt32 (from lab1)
// This will only compile if WrappingInt32 is defined
#ifdef HAS_WRAPPING_INTEGERS
inline std::string to_string(WrappingInt32 i) { return std::to_string(i.raw_value()); }
#endif

// detection idiom: is value stream-insertable
template <class T, class = void>
struct is_streamable : std::false_type {};

template <class T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream &>() << std::declval<T>())>> : std::true_type {};

template <class T>
inline std::enable_if_t<!std::is_arithmetic<T>::value && is_streamable<T>::value, std::string> to_string(
    const T &value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

#endif  // SPONGE_TESTS_STRING_CONVERSIONS_HH
