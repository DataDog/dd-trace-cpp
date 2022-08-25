#pragma once

#include <optional>
#include <variant>

#include "error.h"

namespace datadog {
namespace tracing {

template <typename Value>
class Expected : public std::variant<Value, Error> {
 public:
  using ValueType = Value;

  using std::variant<Value, Error>::variant;
  using std::variant<Value, Error>::operator=;
  using std::variant<Value, Error>::swap;
  using std::variant<Value, Error>::emplace;

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
};

template <typename Value>
bool Expected<Value>::has_value() const noexcept {
  return std::holds_alternative<Value>(*this);
}
template <typename Value>
Expected<Value>::operator bool() const noexcept {
  return has_value();
}

template <typename Value>
Value& Expected<Value>::value() & {
  return std::get<Value>(*this);
}
template <typename Value>
const Value& Expected<Value>::value() const& {
  return std::get<Value>(*this);
}
template <typename Value>
Value&& Expected<Value>::value() && {
  return std::move(std::get<Value>(*this));
}
template <typename Value>
const Value&& Expected<Value>::value() const&& {
  return std::move(std::get<Value>(*this));
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
  return std::get<Error>(*this);
}
template <typename Value>
const Error& Expected<Value>::error() const& {
  return std::get<Error>(*this);
}
template <typename Value>
Error&& Expected<Value>::error() && {
  return std::move(std::get<Error>(*this));
}
template <typename Value>
const Error&& Expected<Value>::error() const&& {
  return std::move(std::get<Error>(*this));
}

template <>
class Expected<void> : public std::optional<Error> {
 public:
  using std::optional<Error>::optional;
  using std::optional<Error>::operator=;
  using std::optional<Error>::swap;
  using std::optional<Error>::emplace;

  bool has_value() const;
  explicit operator bool() const;

  Error& error() &;
  const Error& error() const&;
  Error&& error() &&;
  const Error&& error() const&&;
};

inline bool Expected<void>::has_value() const {
  return !std::optional<Error>::has_value();
}
inline Expected<void>::operator bool() const {
  return Expected<void>::has_value();
}

inline Error& Expected<void>::error() & { return **this; }
inline const Error& Expected<void>::error() const& { return **this; }
inline Error&& Expected<void>::error() && { return std::move(**this); }
inline const Error&& Expected<void>::error() const&& {
  return std::move(**this);
}

}  // namespace tracing
}  // namespace datadog
