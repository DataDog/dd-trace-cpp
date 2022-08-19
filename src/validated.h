#pragma once

#include <utility>
#include <variant>

namespace datadog {
namespace tracing {

class Error;

template <typename Config>
class Validated {
  friend std::variant<Validated<Config>, Error> validate_config(const Config&);

  Config before_env_;
  Config after_env_;

  Validated(const Config& before_env, Config&& after_env);

 public:
  Validated() = delete;

  const Config& operator*() const { return after_env_; }
  Config& operator*() { return after_env_; }
  const Config* operator->() const { return &after_env_; }
  Config* operator->() { return &after_env_; }

  const Config& before_env() const { return before_env_; }
  const Config& after_env() const { return after_env_; }
};

template <typename Config>
Validated<Config>::Validated(const Config& before_env, Config&& after_env)
    : before_env_(before_env), after_env_(std::move(after_env)) {}

}  // namespace tracing
}  // namespace datadog
