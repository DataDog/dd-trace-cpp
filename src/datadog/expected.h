#pragma once

// This component provides a class template, `Expected<T>`, that is either an
// instance of `T` or an instance of `Error`.  `Expected<void>` is either
// `nullopt` or an instance of `Error`.
//
// `Expected` is inspired by, but incompatible with, C++23's `std::expected`.
//
// Example Usage
// -------------
// The following excerpt demonstrates the intended usage of `Expected<T>`:
//
//     Expected<int> parse_integer(StringView name);
//
//
//     int main() {
//       auto maybe_int = parse_integer("the answer");
//       // using the `if_error` method
//       if (auto *error = maybe_int.if_error()) {
//         std::cerr << "parse_integer returned error: " << *error << '\n';
//         return int(error->code);
//       }
//
//       assert(*maybe_int == 42);
//
//       maybe_int = parse_integer("one hundred twenty-three");
//       // use the `error` method.
//       if (!maybe_int) {
//         std::cerr << "parse_integer returned error: " << maybe_int.error() <<
//         '\n'; return int(maybe_int.error().code);
//       }
//
//       assert(maybe_int == 123);
//     }
//
// `Expected<void>` is like `Expected<T>`, except that if the value is not an
// error then it cannot be "dereferenced" with `operator*`, i.e. it is analogous
// to `Optional<Error>` (and is implemented as such).

#include <variant>

#include "error.h"
#include "optional.h"

namespace datadog {
namespace tracing {

template <typename Value>
class Expected {
  std::variant<Value, Error> data_;

 public:
  Expected() = default;
  Expected(const Expected&) = default;
  Expected(Expected&) = default;
  Expected(Expected&&) = default;
  Expected& operator=(const Expected&) = default;
  Expected& operator=(Expected&&) = default;

  template <typename Other>
  Expected(Other&&);
  template <typename Other>
  Expected& operator=(Other&&);

  // Return whether this object holds a `Value` (as opposed to an `Error`).
  bool has_value() const noexcept;
  explicit operator bool() const noexcept;

  // Return a reference to the `Value` held by this object.  If this object is
  // an `Error`, throw a `std::bad_variant_access`.
  Value& value() &;
  const Value& value() const&;
  Value&& value() &&;
  const Value&& value() const&&;
  Value& operator*() &;
  const Value& operator*() const&;
  Value&& operator*() &&;
  const Value&& operator*() const&&;

  // Return a pointer to the `Value` held by this object.  If this object is an
  // `Error`, throw a `std::bad_variant_access`.
  Value* operator->();
  const Value* operator->() const;

  // Return a reference to the `Error` held by this object.  If this object is
  // not an `Error`, throw a `std::bad_variant_access`.
  Error& error() &;
  const Error& error() const&;
  Error&& error() &&;
  const Error&& error() const&&;

  // Return a pointer to the `Error` value held by this object, or return
  // `nullptr` if this object is not an `Error`.
  Error* if_error() &;
  const Error* if_error() const&;
  // Don't use `if_error` on an rvalue (temporary).
  Error* if_error() && = delete;
  const Error* if_error() const&& = delete;
};

template <typename Value>
template <typename Other>
Expected<Value>::Expected(Other&& other) : data_(std::forward<Other>(other)) {}

template <typename Value>
template <typename Other>
Expected<Value>& Expected<Value>::operator=(Other&& other) {
  data_ = std::forward<Other>(other);
  return *this;
}

template <typename Value>
bool Expected<Value>::has_value() const noexcept {
  return std::holds_alternative<Value>(data_);
}
template <typename Value>
Expected<Value>::operator bool() const noexcept {
  return has_value();
}

template <typename Value>
Value& Expected<Value>::value() & {
  return std::get<0>(data_);
}
template <typename Value>
const Value& Expected<Value>::value() const& {
  return std::get<0>(data_);
}
template <typename Value>
Value&& Expected<Value>::value() && {
  return std::move(std::get<0>(data_));
}
template <typename Value>
const Value&& Expected<Value>::value() const&& {
  return std::move(std::get<0>(data_));
}

template <typename Value>
Value& Expected<Value>::operator*() & {
  return value();
}
template <typename Value>
const Value& Expected<Value>::operator*() const& {
  return value();
}
template <typename Value>
Value&& Expected<Value>::operator*() && {
  return std::move(value());
}
template <typename Value>
const Value&& Expected<Value>::operator*() const&& {
  return std::move(value());
}

template <typename Value>
Value* Expected<Value>::operator->() {
  return &value();
}
template <typename Value>
const Value* Expected<Value>::operator->() const {
  return &value();
}

template <typename Value>
Error& Expected<Value>::error() & {
  return std::get<1>(data_);
}
template <typename Value>
const Error& Expected<Value>::error() const& {
  return std::get<1>(data_);
}
template <typename Value>
Error&& Expected<Value>::error() && {
  return std::move(std::get<1>(data_));
}
template <typename Value>
const Error&& Expected<Value>::error() const&& {
  return std::move(std::get<1>(data_));
}

template <typename Value>
Error* Expected<Value>::if_error() & {
  return std::get_if<1>(&data_);
}
template <typename Value>
const Error* Expected<Value>::if_error() const& {
  return std::get_if<1>(&data_);
}

template <>
class Expected<void> {
  Optional<Error> data_;

 public:
  Expected() = default;
  Expected(const Expected&) = default;
  Expected(Expected&) = default;
  Expected(Expected&&) = default;
  Expected& operator=(const Expected&) = default;
  Expected& operator=(Expected&&) = default;

  template <typename Other>
  Expected(Other&&);
  template <typename Other>
  Expected& operator=(Other&&);

  void swap(Expected& other);

  bool has_value() const;
  explicit operator bool() const;

  Error& error() &;
  const Error& error() const&;
  Error&& error() &&;
  const Error&& error() const&&;

  Error* if_error() &;
  const Error* if_error() const&;
  // Don't use `if_error` on an rvalue (temporary).
  Error* if_error() && = delete;
  const Error* if_error() const&& = delete;
};

template <typename Other>
Expected<void>::Expected(Other&& other) : data_(std::forward<Other>(other)) {}

template <typename Other>
Expected<void>& Expected<void>::operator=(Other&& other) {
  data_ = std::forward<Other>(other);
  return *this;
}

inline void Expected<void>::swap(Expected& other) { data_.swap(other.data_); }

inline bool Expected<void>::has_value() const { return !data_.has_value(); }
inline Expected<void>::operator bool() const { return has_value(); }

inline Error& Expected<void>::error() & { return *data_; }
inline const Error& Expected<void>::error() const& { return *data_; }
inline Error&& Expected<void>::error() && { return std::move(*data_); }
inline const Error&& Expected<void>::error() const&& {
  return std::move(*data_);
}

inline Error* Expected<void>::if_error() & { return data_ ? &*data_ : nullptr; }
inline const Error* Expected<void>::if_error() const& {
  return data_ ? &*data_ : nullptr;
}

}  // namespace tracing
}  // namespace datadog
