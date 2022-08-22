#pragma once

#include <utility>
#include <variant>

namespace datadog {
namespace tracing {

class Error;

template <typename Config>
class Validated : public Config {
  friend std::variant<Validated<Config>, Error> validate_config(const Config&);
  template <typename Parent, typename Child>
  friend Validated<Child> bless(Child Parent::*member,
                                const Validated<Parent>& parent);

  explicit Validated(const Config&);
  explicit Validated(Config&&);

 public:
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

}  // namespace tracing
}  // namespace datadog
