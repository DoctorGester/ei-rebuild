#pragma once

// Memberwise copy construction, assignment and resize(n) defaults of game records, the way the compiler wrote
// them for the records' STLport vectors (record_vectors.h).
// More: docs/notes/comments-c22i.md, "record_members.h".

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

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
struct ElementOf<std::string> {
    using type = stlp::ObjectElement<std::string>;
};

inline void CopyMember(mfc::CString& to, const mfc::CString& from) { std::construct_at(&to, from); }
inline void AssignMember(mfc::CString& to, const mfc::CString& from) { to = from; }
inline void DefaultMember(mfc::CString& to) { std::construct_at(&to); }

inline void CopyMember(std::string& to, const std::string& from) { std::construct_at(&to, from); }
inline void AssignMember(std::string& to, const std::string& from) { to = from; }
inline void DefaultMember(std::string& to) { std::construct_at(&to); }

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
template <typename T>
void CopyMember(std::vector<T>& to, const std::vector<T>& from) {
    std::construct_at(&to, from);
}
template <typename T>
void AssignMember(std::vector<T>& to, const std::vector<T>& from) {
    to = from;
}
template <typename T>
void DefaultMember(std::vector<T>& to) {
    std::construct_at(&to);
}

// A vector or a type derived from one (acks::Speeches): copied as a vector, never as bytes.
template <typename U>
std::true_type DerivesVector(const stlp::vector<U>*);
std::false_type DerivesVector(const void*);
template <typename T>
constexpr bool kVectorLike = decltype(DerivesVector(static_cast<T*>(nullptr)))::value;

// Plain data: copied as bytes, left as it is by default. Not the members above, whatever the type
// traits say (an stlp::vector is trivially copyable to them: its copy constructor is deleted, its
// copy assignment the implicit one), nor types derived from a vector, nor arrays of them. A trivially copyable record with copies of
// its own (CopyMember / AssignMember / DefaultMember found by argument-dependent lookup) specialises
// this to false, so arrays of it go element by element too.
template <typename T>
struct IsPlain : std::bool_constant<std::is_trivially_copyable_v<T> && !kVectorLike<T>> {};
template <>
struct IsPlain<mfc::CString> : std::false_type {};
template <>
struct IsPlain<std::string> : std::false_type {};
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
static_assert(!kPlain<mfc::CString[2][3]> && kPlain<int[4]> && !kPlain<std::vector<int>[2]>);
struct DerivedVectorCheck : stlp::vector<int> {};
static_assert(!kPlain<DerivedVectorCheck> && !kPlain<DerivedVectorCheck[2]>, "derived vectors are not bytes");

// The copy constructor, operator= and resize(n) default of R over the members M (member pointers,
// declaration order). The destructor is the record's own.
template <typename R, auto... M>
struct Memberwise {
    static void Construct(R* to, const R& from) { (CopyMember(to->*M, from.*M), ...); }
    static void Assign(R& to, const R& from) { (AssignMember(to.*M, from.*M), ...); }
    static void Default(R* p) { (DefaultMember(p->*M), ...); }
};

} // namespace records
