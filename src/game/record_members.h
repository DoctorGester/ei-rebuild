#pragma once

// Memberwise copy construction, assignment and resize(n) defaults of game records, the way the
// compiler wrote them for the records' STLport vectors (S10a; record_vectors.h). A record lists
// the members its copy constructor and operator= touch, in declaration order:
//   CString            copy-constructed / assigned (007006E5 / 00700AA9), empty by default;
//   stlp::string       copy-constructed / AssignFrom, by default empty with room for 8 bytes
//                      (00421800 / 0041F460 with 8);
//   stlp::vector<T>    copy-constructed (room for exactly the size) / operator=, empty by default,
//                      its elements by the policy of T (plain bytes, CString, stlp::string);
//   arrays             element by element;
//   plain data         copied as bytes, left as it is by default (the original's stack
//                      temporaries leave it uninitialised).
// Members a record's copies skip (padding, bytes no constructor sets) are simply not listed.

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace records {

template <typename T>
struct ElementOf {
    using type = stlp::PlainElement<T>;
};
template <>
struct ElementOf<mfc::CString> {
    using type = stlp::ObjectElement<mfc::CString>;
};
template <>
struct ElementOf<stlp::string> {
    using type = stlp::StringElement;
};

inline void CopyMember(mfc::CString& to, const mfc::CString& from) { new (&to) mfc::CString(from); }
inline void AssignMember(mfc::CString& to, const mfc::CString& from) { to = from; }
inline void DefaultMember(mfc::CString& to) { new (&to) mfc::CString(); }

inline void CopyMember(stlp::string& to, const stlp::string& from) { new (&to) stlp::string(from); }
inline void AssignMember(stlp::string& to, const stlp::string& from) { to.AssignFrom(from); }
inline void DefaultMember(stlp::string& to) { stlp::StringElement::Default(&to); }

template <typename T>
void CopyMember(stlp::vector<T>& to, const stlp::vector<T>& from) {
    to.template CopyConstruct<typename ElementOf<T>::type>(from);
}
template <typename T>
void AssignMember(stlp::vector<T>& to, const stlp::vector<T>& from) {
    to.template AssignVector<typename ElementOf<T>::type>(from);
}
template <typename T>
void DefaultMember(stlp::vector<T>& to) {
    to.ConstructEmpty();
}

// Plain data: copied as bytes, left as it is by default. Not the members above, whatever the type
// traits say (an stlp::vector is trivially copyable to them: its copy constructor is deleted, its
// copy assignment the implicit one), nor arrays of them. A trivially copyable record with copies of
// its own (CopyMember / AssignMember / DefaultMember found by argument-dependent lookup) specialises
// this to false, so arrays of it go element by element too.
template <typename T>
struct IsPlain : std::bool_constant<std::is_trivially_copyable_v<T>> {};
template <>
struct IsPlain<mfc::CString> : std::false_type {};
template <>
struct IsPlain<stlp::string> : std::false_type {};
template <typename T>
struct IsPlain<stlp::vector<T>> : std::false_type {};
template <typename T, std::size_t N>
struct IsPlain<T[N]> : IsPlain<T> {};
template <typename T>
constexpr bool kPlain = IsPlain<T>::value;

template <typename T>
std::enable_if_t<kPlain<T>> CopyMember(T& to, const T& from) {
    std::memcpy(&to, &from, sizeof(T));
}
template <typename T>
std::enable_if_t<kPlain<T>> AssignMember(T& to, const T& from) {
    std::memcpy(&to, &from, sizeof(T));
}
template <typename T>
std::enable_if_t<kPlain<T>> DefaultMember(T&) {}

// Arrays of the rest: element by element (an element type with no policy does not compile).
template <typename T, std::size_t N>
std::enable_if_t<!kPlain<T>> CopyMember(T (&to)[N], const T (&from)[N]) {
    for (std::size_t i = 0; i < N; ++i) CopyMember(to[i], from[i]);
}
template <typename T, std::size_t N>
std::enable_if_t<!kPlain<T>> AssignMember(T (&to)[N], const T (&from)[N]) {
    for (std::size_t i = 0; i < N; ++i) AssignMember(to[i], from[i]);
}
template <typename T, std::size_t N>
std::enable_if_t<!kPlain<T>> DefaultMember(T (&to)[N]) {
    for (std::size_t i = 0; i < N; ++i) DefaultMember(to[i]);
}

static_assert(std::is_trivially_copyable_v<stlp::vector<int>[2]> && !kPlain<stlp::vector<int>[2]>,
              "arrays of vectors go element by element");
static_assert(!kPlain<mfc::CString[2][3]> && kPlain<int[4]>);

// The copy constructor, operator= and resize(n) default of R over the members M (member pointers,
// declaration order). The destructor is the record's own (its S13 or earlier port).
template <typename R, auto... M>
struct Memberwise {
    static void Construct(R* to, const R& from) { (CopyMember(to->*M, from.*M), ...); }
    static void Assign(R& to, const R& from) { (AssignMember(to.*M, from.*M), ...); }
    static void Default(R* p) { (DefaultMember(p->*M), ...); }
};

} // namespace records
