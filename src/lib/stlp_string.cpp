// STLport basic_string<char> members (stlp_string.h) registered at the library addresses.
#include "stlp_string.h"

#include "lib/crt.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>

namespace stlp {

void __fastcall StringAbi::ConstructAt(string* p, const string* from) {
    if (p) p->InitRange(from->start_, from->finish_);
}

string* __fastcall StringAbi::ConstructDefault(string* self) {
    self->start_ = self->finish_ = allocate_as<char>(8);
    self->eos_ = self->start_ + 8;
    *self->finish_ = 0;
    return self;
}

string string::substr(std::size_t pos, std::size_t n) const {
    std::size_t size = this->size();
    if (pos > size) ThrowOutOfRange();
    std::size_t length = std::min(size - pos, n);
    return string(start_ + pos, start_ + pos + length);
}

bool __fastcall StringAbi::Less(const string* a, const string* b) { return *a < *b; }

string* StringAbi::DeletingDestructor(unsigned flags) {
    Release();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

} // namespace stlp

using stlp::StringAbi;

namespace stlp {

namespace {

// STLport's out_of_range (a class of ours, thrown by the compiler with its own throw information in place of
// game.exe's ThrowInfo, catchable types and type descriptors, and of its tables):
// std::exception, __Named_exception's 256-byte name (its what), logic_error. Nothing catches one by type.
class NamedException : public std::exception {
public:
    // The text copied into the name (strncpy, the last byte zero).
    explicit NamedException(const string& what) {
        gamecrt::strncpy(name_, what.data(), sizeof(name_));
        name_[sizeof(name_) - 1] = 0;
    }
    const char* what() const noexcept override { return name_; }

private:
    char name_[0x100];
};
class LogicError : public NamedException {
public:
    using NamedException::NamedException;
};
class OutOfRange : public LogicError {
public:
    using LogicError::LogicError;
};

} // namespace

void __fastcall ThrowOutOfRange() {
    const string what("basic_string");
    throw OutOfRange(what);
}

} // namespace stlp
