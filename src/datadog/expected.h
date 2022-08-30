#pragma once

#include <optional>
#include <variant>

#include "error.h"

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

  bool has_value() const noexcept;
  explicit operator bool() const noexcept;

  Value& value() &;
  const Value& value() const&;
  Value&& value() &&;
  const Value&& value() const&&;

  Value& operator*() &;
  const Value& operator*() const&;
  Value&& operator*() &&;
  const Value&& operator*() const&&;

  Value* operator->();
  const Value* operator->() const;

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
  std::optional<Error> data_;

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
