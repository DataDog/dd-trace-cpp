#pragma once

#include <utility>
#include <variant>

#include "expected.h"

namespace datadog {
namespace tracing {

template <typename Config>
class Validated : public Config {
  friend Expected<Validated<Config>> validate_config(const Config&);
  template <typename Parent, typename Child>
  friend Validated<Child> bless(Child Parent::*member,
                                const Validated<Parent>& parent);
  template <typename Parent, typename Child, typename... Alternatives>
  friend Validated<Child> bless(
      std::variant<Child, Alternatives...> Parent::*member,
      const Validated<Parent>& parent);

  explicit Validated(const Config&);
  explicit Validated(Config&&);

  Validated() = delete;
};

template <typename Config>
Validated<Config>::Validated(const Config& config) : Config(config) {}

template <typename Config>
Validated<Config>::Validated(Config&& config) : Config(std::move(config)) {}

template <typename Parent, typename Child>
Validated<Child> bless(Child Parent::*member, const Validated<Parent>& parent) {
  return Validated<Child>{parent.*member};
}

template <typename Parent, typename Child, typename... Alternatives>
Validated<Child> bless(std::variant<Child, Alternatives...> Parent::*member,
                       const Validated<Parent>& parent) {
  return Validated<Child>{std::get<Child>(parent.*member)};
}

}  // namespace tracing
}  // namespace datadog
