#include <datadog/id_generator.h>
#include <datadog/optional.h>
#include <datadog/propagation_style.h>
#include <datadog/threaded_event_scheduler.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>

#include "datadog/span_sampler_config.h"
#include "datadog/trace_sampler_config.h"
#include "mocks/collectors.h"
#include "mocks/event_schedulers.h"
#include "mocks/loggers.h"
#include "test.h"
#ifdef _MSC_VER
#include <winbase.h>  // SetEnvironmentVariable
#else
#include <stdlib.h>  // setenv, unsetenv
#endif

namespace datadog {
namespace tracing {

std::ostream& operator<<(std::ostream& stream, PropagationStyle style) {
  return stream << to_json(style).dump();
}

}  // namespace tracing
}  // namespace datadog

using namespace datadog::tracing;
using namespace std::chrono_literals;

namespace {

// For the lifetime of this object, set a specified environment variable.
// Restore any previous value (or unset the value if it was unset) afterward.
class EnvGuard {
  std::string name_;
  Optional<std::string> former_value_;

 public:
  EnvGuard(std::string name, std::string value) : name_(std::move(name)) {
    const char* current = std::getenv(name_.c_str());
    if (current) {
      former_value_ = current;
    }
    set_value(value);
  }

  ~EnvGuard() {
    if (former_value_) {
      set_value(*former_value_);
    } else {
      unset();
    }
  }

  void set_value(const std::string& value) {
#ifdef _MSC_VER
    ::SetEnvironmentVariable(name_.c_str(), value.c_str());
#else
    const bool overwrite = true;
    ::setenv(name_.c_str(), value.c_str(), overwrite);
#endif
  }

  void unset() {
#ifdef _MSC_VER
    ::SetEnvironmentVariable(name_.c_str(), NULL);
#else
    ::unsetenv(name_.c_str());
#endif
  }
};

// For brevity when we're tabulating a lot of test cases with parse
// `Optional<...>` data members.
const auto x = nullopt;

// Here's an attempt at a portable secure temporary file.
// There's no standard solution, and it's generally hard on Windows.
class SomewhatSecureTemporaryFile : public std::fstream {
  std::filesystem::path path_;

 public:
  SomewhatSecureTemporaryFile() try {
    namespace fs = std::filesystem;

    const auto generator = default_id_generator(false);
    const auto random = [&]() { return generator->span_id(); };

    // The goal is to create a file whose name is like
    // "/tmp/342394898324/239489029034", where the directory under /tmp has
    // permissions such that only the current user can read/write/cd it.
    const auto tmp = fs::temp_directory_path();
    const int max_attempts = 5;
    for (int i = 0; i < max_attempts; ++i) {
      const auto dir = tmp / std::to_string(random());
      std::error_code err;
      if (!fs::create_directory(dir, err)) {
        continue;
      }
      fs::permissions(dir, fs::perms(0700), fs::perm_options::replace, err);
      if (err) {
        continue;
      }
      const auto file = dir / std::to_string(random());
      if (fs::exists(file, err) || err) {
        continue;
      }
      // We did it!
      open(file, std::ios::in | std::ios::out | std::ios::app);
      path_ = file;
      return;
    }
    throw std::runtime_error("exhausted all attempts");
  } catch (const std::exception& error) {
    std::cerr << "Unable to create a temporary file: " << error.what() << '\n';
    // `path_` is empty, and this `fstream` is not open.
  }

  ~SomewhatSecureTemporaryFile() {
    if (path_ != std::filesystem::path{}) {
      std::error_code ignored;
      std::filesystem::remove_all(path_.parent_path(), ignored);
    }
  }

  const std::filesystem::path& path() const { return path_; }
};

}  // namespace

TEST_CASE("TracerConfig::defaults") {
  TracerConfig config;

  SECTION("service is required") {
    SECTION("empty") {
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::SERVICE_NAME_REQUIRED);
    }
    SECTION("nonempty") {
      config.set_service_name("testsvc");
      auto finalized = config.finalize();
      REQUIRE(finalized);
    }
  }

  SECTION("DD_SERVICE overrides service") {
    const EnvGuard guard{"DD_SERVICE", "foosvc"};
    config.set_service_name("testsvc");
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->defaults.service == "foosvc");
  }

  SECTION("DD_ENV overrides environment") {
    const EnvGuard guard{"DD_ENV", "prod"};
    config.set_environment("dev");
    config.set_service_name("required");
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->defaults.environment == "prod");
  }

  SECTION("DD_VERSION overrides version") {
    const EnvGuard guard{"DD_VERSION", "v2"};
    config.set_version("v1");
    config.set_service_name("required");
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->defaults.version == "v2");
  }

  SECTION("DD_TAGS") {
    struct TestCase {
      std::string name;
      std::string dd_tags;
      std::unordered_map<std::string, std::string> expected_tags;
      Optional<Error::Code> expected_error;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"empty", "", {}, nullopt},
        {"missing colon", "foo", {}, Error::TAG_MISSING_SEPARATOR},
        {"trailing comma",
         "foo:bar, baz:123,",
         {},
         Error::TAG_MISSING_SEPARATOR},
        {"overwrite value", "foo:baz", {{"foo", "baz"}}, nullopt},
        {"additional values",
         "baz:123, bam:three",
         {{"baz", "123"}, {"bam", "three"}},
         nullopt},
        {"commas optional",
         "baz:123 bam:three",
         {{"baz", "123"}, {"bam", "three"}},
         nullopt},
        {"last one wins", "baz:123 baz:three", {{"baz", "three"}}, nullopt},
    }));

    // This will be overriden by the DD_TAGS environment variable.
    config.set_tags({{"foo", "bar"}});
    config.set_service_name("required");

    CAPTURE(test_case.name);
    const EnvGuard guard{"DD_TAGS", test_case.dd_tags};
    auto finalized = config.finalize();
    if (test_case.expected_error) {
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == *test_case.expected_error);

    } else {
      REQUIRE(finalized);
      REQUIRE(finalized->defaults.tags == test_case.expected_tags);
    }
  }
}

TEST_CASE("TracerConfig::log_on_startup") {
  TracerConfig config;
  config.set_service_name("testsvc");
  const auto logger = std::make_shared<MockLogger>();
  config.set_logger(logger);

  SECTION("default is true") {
    {
      auto finalized = config.finalize();
      REQUIRE(finalized);
      const Tracer tracer{*finalized};
      (void)tracer;
    }
    REQUIRE(logger->startup_count() == 1);
    // This check is weak, but better than nothing.
    REQUIRE(logger->first_startup().size() > 0);
  }

  SECTION("false silences the startup message") {
    {
      config.log_configuration_on_startup(false);
      auto finalized = config.finalize();
      REQUIRE(finalized);
      const Tracer tracer{*finalized};
      (void)tracer;
    }
    REQUIRE(logger->startup_count() == 0);
  }

  SECTION("overridden by DD_TRACE_STARTUP_LOGS") {
    struct TestCase {
      std::string name;
      std::string dd_trace_startup_logs;
      bool expect_startup_log;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"DD_TRACE_STARTUP_LOGS=''", "", true},
        {"DD_TRACE_STARTUP_LOGS='0'", "0", false},
        {"DD_TRACE_STARTUP_LOGS='false'", "false", false},
        {"DD_TRACE_STARTUP_LOGS='FaLsE'", "FaLsE", false},
        {"DD_TRACE_STARTUP_LOGS='no'", "no", false},
        {"DD_TRACE_STARTUP_LOGS='n'", "n", true},
        {"DD_TRACE_STARTUP_LOGS='1'", "1", true},
        {"DD_TRACE_STARTUP_LOGS='true'", "true", true},
        {"DD_TRACE_STARTUP_LOGS='goldfish'", "goldfish", true},
    }));

    CAPTURE(test_case.name);
    const EnvGuard guard{"DD_TRACE_STARTUP_LOGS",
                         test_case.dd_trace_startup_logs};
    {
      auto finalized = config.finalize();
      REQUIRE(finalized);
      const Tracer tracer{*finalized};
      (void)tracer;
    }
    REQUIRE(logger->startup_count() == int(test_case.expect_startup_log));
  }
}

TEST_CASE("TracerConfig::report_traces") {
  TracerConfig config;
  config.set_service_name("testsvc");
  const auto collector = std::make_shared<MockCollector>();
  config.set_collector(collector);
  config.set_logger(std::make_shared<NullLogger>());

  SECTION("default is true") {
    {
      auto finalized = config.finalize();
      REQUIRE(finalized);
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(collector->chunks.size() == 1);
    REQUIRE(collector->chunks.front().size() == 1);
  }

  SECTION("false disables collection") {
    {
      config.enable_traces(false);
      auto finalized = config.finalize();
      REQUIRE(finalized);
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(collector->chunks.size() == 0);
  }

  SECTION("overridden by DD_TRACE_ENABLED") {
    struct TestCase {
      std::string name;
      std::string dd_trace_enabled;
      bool original_value;
      bool expect_spans;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"falsy override ('false')", "false", true, false},
        {"falsy override ('0')", "0", true, false},
        {"falsy consistent ('false')", "false", false, false},
        {"falsy consistent ('0')", "0", false, false},
        {"truthy override ('true')", "true", false, true},
        {"truthy override ('1')", "1", false, true},
        {"truthy consistent ('true')", "true", true, true},
        {"truthy consistent ('1')", "1", true, true},
    }));

    CAPTURE(test_case.name);
    const EnvGuard guard{"DD_TRACE_ENABLED", test_case.dd_trace_enabled};
    config.enable_traces(test_case.original_value);
    {
      auto finalized = config.finalize();
      REQUIRE(finalized);
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    if (test_case.expect_spans) {
      REQUIRE(collector->chunks.size() == 1);
      REQUIRE(collector->chunks.front().size() == 1);
    } else {
      REQUIRE(collector->chunks.size() == 0);
    }
  }
}

TEST_CASE("TracerConfig::agent") {
  TracerConfig config;
  config.set_service_name("testsvc");

  SECTION("event_scheduler") {
    SECTION("default") {
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(dynamic_cast<ThreadedEventScheduler*>(
          finalized->agent_config.event_scheduler.get()));
    }

    SECTION("custom") {
      auto scheduler = std::make_shared<MockEventScheduler>();
      config.set_event_scheduler(scheduler);
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->agent_config.event_scheduler == scheduler);
    }
  }

  SECTION("flush interval") {
    SECTION("cannot be zero") {
      config.set_flush_interval(0ms);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code ==
              Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL);
    }

    SECTION("cannot be negative") {
      config.set_flush_interval(-1337ms);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code ==
              Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL);
    }
  }

  SECTION("remote configuration poll interval") {
    SECTION("cannot be zero") {
      config.set_remote_configuration_poll_interval(0s);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code ==
              Error::DATADOG_AGENT_INVALID_REMOTE_CONFIG_POLL_INTERVAL);
    }

    SECTION("cannot be negative") {
      config.set_remote_configuration_poll_interval(-1337s);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code ==
              Error::DATADOG_AGENT_INVALID_REMOTE_CONFIG_POLL_INTERVAL);
    }

    SECTION("override default value") {
      SECTION("environment variable") {
        const EnvGuard env_guard{"DD_REMOTE_CONFIG_POLL_INTERVAL_SECONDS",
                                 "15"};
        auto finalized = config.finalize();
        REQUIRE(finalized);
        REQUIRE(finalized->agent_config.remote_configuration_poll_interval ==
                std::chrono::seconds(15));
      }

      SECTION("ill-formated environment variable is an error") {
        const EnvGuard env_guard{"DD_REMOTE_CONFIG_POLL_INTERVAL_SECONDS",
                                 "ddog"};
        auto finalized = config.finalize();
        REQUIRE(!finalized);
        REQUIRE(finalized.error().code == Error::INVALID_INTEGER);
      }
    }
  }

  SECTION("url") {
    SECTION("parsing") {
      struct TestCase {
        std::string url;
        Optional<Error::Code> expected_error;
        std::string expected_scheme = "";
        std::string expected_authority = "";
        std::string expected_path = "";
      };

      auto test_case = GENERATE(values<TestCase>({
          {"http://dd-agent:8126", nullopt, "http", "dd-agent:8126", ""},
          {"http://dd-agent:8126/", nullopt, "http", "dd-agent:8126", "/"},
          {"https://dd-agent:8126/", nullopt, "https", "dd-agent:8126", "/"},
          {"unix:///var/run/datadog/trace-agent.sock", nullopt, "unix",
           "/var/run/datadog/trace-agent.sock"},
          {"unix://var/run/datadog/trace-agent.sock",
           Error::URL_UNIX_DOMAIN_SOCKET_PATH_NOT_ABSOLUTE},
          {"http+unix:///run/datadog/trace-agent.sock", nullopt, "http+unix",
           "/run/datadog/trace-agent.sock"},
          {"https+unix:///run/datadog/trace-agent.sock", nullopt, "https+unix",
           "/run/datadog/trace-agent.sock"},
          {"tcp://localhost:8126", Error::URL_UNSUPPORTED_SCHEME},
          {"/var/run/datadog/trace-agent.sock", Error::URL_MISSING_SEPARATOR},
      }));

      CAPTURE(test_case.url);
      config.set_datadog_agent_url(test_case.url);
      auto finalized = config.finalize();
      if (test_case.expected_error) {
        REQUIRE(!finalized);
        REQUIRE(finalized.error().code == *test_case.expected_error);
      } else {
        REQUIRE(finalized);

        const auto& url = finalized->agent_config.url;
        REQUIRE(url.scheme == test_case.expected_scheme);
        REQUIRE(url.authority == test_case.expected_authority);
        REQUIRE(url.path == test_case.expected_path);
      }
    }

    SECTION("environment variables override") {
      struct TestCase {
        std::string name;
        Optional<std::string> env_host;
        Optional<std::string> env_port;
        Optional<std::string> env_url;
        std::string expected_scheme;
        std::string expected_authority;
      };

      auto test_case = GENERATE(values<TestCase>({
          {"override host with default port", "dd-agent", x, x, "http",
           "dd-agent:8126"},
          {"override port and host", "dd-agent", "8080", x, "http",
           "dd-agent:8080"},
          {"override port with default host", x, "8080", x, "http",
           "localhost:8080"},
          // A bogus port number will cause an error in the TCPClient, not
          // during configuration.  For the purposes of configuration, any
          // value is accepted.
          {"we don't parse port", x, "bogus", x, "http", "localhost:bogus"},
          {"even empty is ok", x, "", x, "http", "localhost:"},
          {"URL", x, x, "http://dd-agent:8080", "http", "dd-agent:8080"},
          {"URL overrides scheme", x, x, "https://dd-agent:8080", "https",
           "dd-agent:8080"},
          {"URL overrides host", "localhost", x, "http://dd-agent:8080", "http",
           "dd-agent:8080"},
          {"URL overrides port", x, "8126", "http://dd-agent:8080", "http",
           "dd-agent:8080"},
          {"URL overrides port and host", "localhost", "8126",
           "http://dd-agent:8080", "http", "dd-agent:8080"},
      }));

      CAPTURE(test_case.name);
      Optional<EnvGuard> host_guard;
      if (test_case.env_host) {
        host_guard.emplace("DD_AGENT_HOST", *test_case.env_host);
      }
      Optional<EnvGuard> port_guard;
      if (test_case.env_port) {
        port_guard.emplace("DD_TRACE_AGENT_PORT", *test_case.env_port);
      }
      Optional<EnvGuard> url_guard;
      if (test_case.env_url) {
        url_guard.emplace("DD_TRACE_AGENT_URL", *test_case.env_url);
      }

      auto finalized = config.finalize();
      REQUIRE(finalized);

      const auto& url = finalized->agent_config.url;
      REQUIRE(url.scheme == test_case.expected_scheme);
      REQUIRE(url.authority == test_case.expected_authority);
    }
  }
}

TEST_CASE("TracerConfig::trace_sampler") {
  TracerConfig config;
  config.set_service_name("testsvc");

  SECTION("default is no rules") {
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_sampler.rules.size() == 0);
  }

  SECTION("one sampling rule") {
    TraceSamplerConfig trace_sampler;
    auto& rules = trace_sampler.rules;
    rules.resize(rules.size() + 1);
    TraceSamplerConfig::Rule& rule = rules.back();

    SECTION("yields one sampling rule") {
      config.set_trace_sampler(trace_sampler);
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->trace_sampler.rules.size() == 1);
      // and the default sample_rate is 100%
      REQUIRE(finalized->trace_sampler.rules.front().sample_rate == 1.0);
    }

    SECTION("has to have a valid sample_rate") {
      auto rate = GENERATE(std::nan(""), -0.5, 1.3,
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), 42);
      rule.sample_rate = rate;
      config.set_trace_sampler(trace_sampler);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::RATE_OUT_OF_RANGE);
    }
  }

  SECTION("two sampling rules") {
    TraceSamplerConfig trace_sampler;
    auto& rules = trace_sampler.rules;
    rules.resize(rules.size() + 2);
    rules[0].sample_rate = 0.5;
    rules[1].sample_rate = 0.6;

    config.set_trace_sampler(trace_sampler);
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_sampler.rules.size() == 2);
    REQUIRE(finalized->trace_sampler.rules[0].sample_rate == 0.5);
    REQUIRE(finalized->trace_sampler.rules[1].sample_rate == 0.6);
  }

  SECTION("global sample_rate creates a catch-all rule") {
    TraceSamplerConfig trace_sampler;
    trace_sampler.sample_rate = 0.25;

    config.set_trace_sampler(trace_sampler);
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_sampler.rules.size() == 1);
    const auto& rule = finalized->trace_sampler.rules.front();
    REQUIRE(rule.sample_rate == 0.25);
    REQUIRE(rule.service == "*");
    REQUIRE(rule.name == "*");
    REQUIRE(rule.resource == "*");
    REQUIRE(rule.tags.empty());
  }

  SECTION("DD_TRACE_SAMPLE_RATE") {
    SECTION("sets the global sample_rate") {
      const EnvGuard guard{"DD_TRACE_SAMPLE_RATE", "0.5"};
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->trace_sampler.rules.size() == 1);
      REQUIRE(finalized->trace_sampler.rules.front().sample_rate == 0.5);
    }

    SECTION("overrides TraceSamplerConfig::sample_rate") {
      TraceSamplerConfig trace_sampler;
      trace_sampler.sample_rate = 0.25;

      config.set_trace_sampler(trace_sampler);
      const EnvGuard guard{"DD_TRACE_SAMPLE_RATE", "0.5"};
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->trace_sampler.rules.size() == 1);
      REQUIRE(finalized->trace_sampler.rules.front().sample_rate == 0.5);
    }

    SECTION("has to have a valid value") {
      struct TestCase {
        std::string name;
        std::string env_value;
        std::vector<Error::Code> allowed_errors;
      };

      auto test_case = GENERATE(values<TestCase>({
          {"empty", "", {Error::INVALID_DOUBLE}},
          {"nonsense", "nonsense", {Error::INVALID_DOUBLE}},
          {"trailing space", "0.23   ", {Error::INVALID_DOUBLE}},
          {"out of range of double", "123e9999999999", {Error::INVALID_DOUBLE}},
          // Some C++ standard libraries parse "nan" and "inf" as the
          // corresponding special floating point values. Other standard
          // libraries consider "nan" and "inf" invalid.
          // So, either the double will fail to parse, or parsing will succeed
          // but the resulting value will be outside of the inclusive range
          // [0.0, 1.0] of the `Rate` type.
          {"NaN", "NaN", {Error::INVALID_DOUBLE, Error::RATE_OUT_OF_RANGE}},
          {"nan", "nan", {Error::INVALID_DOUBLE, Error::RATE_OUT_OF_RANGE}},
          {"inf", "inf", {Error::INVALID_DOUBLE, Error::RATE_OUT_OF_RANGE}},
          {"Inf", "Inf", {Error::INVALID_DOUBLE, Error::RATE_OUT_OF_RANGE}},
          {"below range", "-0.1", {Error::RATE_OUT_OF_RANGE}},
          {"above range", "1.1", {Error::RATE_OUT_OF_RANGE}},
      }));

      CAPTURE(test_case.name);

      const EnvGuard guard{"DD_TRACE_SAMPLE_RATE", test_case.env_value};
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE_THAT(test_case.allowed_errors,
                   Catch::Matchers::VectorContains(finalized.error().code));
    }
  }

  SECTION("max_per_second") {
    SECTION("defaults to 200") {
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->trace_sampler.max_per_second == 200);
    }

    SECTION("must be >0 and a finite number") {
      auto limit = GENERATE(0.0, -1.0, std::nan(""),
                            std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity());

      CAPTURE(limit);
      CAPTURE(std::fpclassify(limit));

      TraceSamplerConfig trace_sampler;
      trace_sampler.max_per_second = limit;

      config.set_trace_sampler(trace_sampler);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::MAX_PER_SECOND_OUT_OF_RANGE);
    }
  }

  SECTION("DD_TRACE_RATE_LIMIT") {
    SECTION("overrides SpanSamplerConfig::max_per_second") {
      const EnvGuard guard{"DD_TRACE_RATE_LIMIT", "120"};
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->trace_sampler.max_per_second == 120);
    }

    SECTION("has to have a valid value") {
      struct TestCase {
        std::string name;
        std::string env_value;
        std::vector<Error::Code> allowed_errors;
      };

      auto test_case = GENERATE(values<TestCase>({
          {"empty", "", {Error::INVALID_DOUBLE}},
          {"nonsense", "nonsense", {Error::INVALID_DOUBLE}},
          {"trailing space", "23   ", {Error::INVALID_DOUBLE}},
          {"out of range of double", "123e9999999999", {Error::INVALID_DOUBLE}},
          // Some C++ standard libraries parse "nan" and "inf" as the
          // corresponding special floating point values. Other standard
          // libraries consider "nan" and "inf" invalid.
          // So, either the double will fail to parse, or parsing will succeed
          // but the resulting value will be outside of the exclusive range
          // (0.0, Inf) allowed.
          {"NaN",
           "NaN",
           {Error::INVALID_DOUBLE, Error::MAX_PER_SECOND_OUT_OF_RANGE}},
          {"nan",
           "nan",
           {Error::INVALID_DOUBLE, Error::MAX_PER_SECOND_OUT_OF_RANGE}},
          {"inf",
           "inf",
           {Error::INVALID_DOUBLE, Error::MAX_PER_SECOND_OUT_OF_RANGE}},
          {"Inf",
           "Inf",
           {Error::INVALID_DOUBLE, Error::MAX_PER_SECOND_OUT_OF_RANGE}},
          {"below range", "-0.1", {Error::MAX_PER_SECOND_OUT_OF_RANGE}},
          {"zero (also below range)",
           "0",
           {Error::MAX_PER_SECOND_OUT_OF_RANGE}},
      }));

      CAPTURE(test_case.name);

      const EnvGuard guard{"DD_TRACE_RATE_LIMIT", test_case.env_value};
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE_THAT(test_case.allowed_errors,
                   Catch::Matchers::VectorContains(finalized.error().code));
    }
  }

  SECTION("DD_TRACE_SAMPLING_RULES") {
    SECTION("sets sampling rules and overrides TraceSampler::rules") {
      TraceSamplerConfig::Rule config_rule;
      config_rule.service = "whatever";

      TraceSamplerConfig trace_sampler;
      trace_sampler.rules.push_back(config_rule);

      config.set_trace_sampler(trace_sampler);

      auto rules_json = R"json([
        {"service": "poohbear", "name": "get.honey", "sample_rate": 0},
        {"tags": {"error": "*"}, "resource": "/admin/*"}
      ])json";

      const EnvGuard guard{"DD_TRACE_SAMPLING_RULES", rules_json};
      auto finalized = config.finalize();
      REQUIRE(finalized);

      const auto& rules = finalized->trace_sampler.rules;
      CAPTURE(rules_json);
      CAPTURE(rules);
      REQUIRE(rules.size() == 2);
      REQUIRE(rules[0].service == "poohbear");
      REQUIRE(rules[0].name == "get.honey");
      REQUIRE(rules[0].sample_rate == 0);
      REQUIRE(rules[0].tags.size() == 0);
      REQUIRE(rules[1].service == "*");
      REQUIRE(rules[1].name == "*");
      REQUIRE(rules[1].sample_rate == 1);
      REQUIRE(rules[1].tags.size() == 1);
      REQUIRE(rules[1].tags.at("error") == "*");
      REQUIRE(rules[1].resource == "/admin/*");
    }

    SECTION("must be valid") {
      struct TestCase {
        std::string name;
        std::string json;
        Error::Code expected_error;
      };

      auto test_case = GENERATE(values<TestCase>({
          {"invalid JSON", "this is clearly not JSON",
           Error::TRACE_SAMPLING_RULES_INVALID_JSON},
          {"barely not JSON", "[true,]",
           Error::TRACE_SAMPLING_RULES_INVALID_JSON},
          {"must be array",
           R"json({"service": "you forgot the square brackets"})json",
           Error::TRACE_SAMPLING_RULES_WRONG_TYPE},
          {"service must be a string", R"json([{"service": 123}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"name must be a string", R"json([{"name": null}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"resource must be a string", R"json([{"resource": false}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"'tags' property must be an object",
           R"json([{"tags": ["foo:bar"]}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"tag values must be strings",
           R"json([{"tags": {"foo": "two", "error": false}}])json",
           Error::RULE_TAG_WRONG_TYPE},
          {"each rule must be an object", R"json([["service", "wrong!"]])json",
           Error::RULE_WRONG_TYPE},
          {"sample_rate must be a number", R"json([{"sample_rate": true}])json",
           Error::TRACE_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE},
          {"no unknown properties", R"json([{"extension": "denied!"}])json",
           Error::TRACE_SAMPLING_RULES_UNKNOWN_PROPERTY},
      }));

      CAPTURE(test_case.name);

      const EnvGuard guard{"DD_TRACE_SAMPLING_RULES", test_case.json};
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == test_case.expected_error);
    }
  }
}

TEST_CASE("TracerConfig::span_sampler") {
  TracerConfig config;
  config.set_service_name("testsvc");

  SECTION("default is no rules") {
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->span_sampler.rules.size() == 0);
  }

  SECTION("one sampling rule") {
    SpanSamplerConfig span_sampler;
    auto& rules = span_sampler.rules;
    rules.resize(rules.size() + 1);
    SpanSamplerConfig::Rule& rule = rules.back();

    SECTION("yields one sampling rule") {
      config.set_span_sampler(span_sampler);
      auto finalized = config.finalize();
      REQUIRE(finalized);
      REQUIRE(finalized->span_sampler.rules.size() == 1);
      // the default sample_rate is 100%
      REQUIRE(finalized->span_sampler.rules.front().sample_rate == 1.0);
      // the default max_per_second is null (unlimited)
      REQUIRE(!finalized->span_sampler.rules.front().max_per_second);
    }

    SECTION("has to have a valid sample_rate") {
      auto rate = GENERATE(std::nan(""), -0.5, 1.3,
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), 42);
      CAPTURE(rate);
      rule.sample_rate = rate;

      config.set_span_sampler(span_sampler);
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::RATE_OUT_OF_RANGE);
    }

    SECTION("has to have a valid max_per_second (if not null)") {
      auto limit =
          GENERATE(0.0, -1.0, std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity(), std::nan(""));
      CAPTURE(limit);
      rule.max_per_second = limit;

      config.set_span_sampler(span_sampler);

      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::MAX_PER_SECOND_OUT_OF_RANGE);
    }
  }

  SECTION("two sampling rules") {
    SpanSamplerConfig span_sampler;
    auto& rules = span_sampler.rules;
    rules.resize(rules.size() + 2);
    rules[0].sample_rate = 0.5;
    rules[1].sample_rate = 0.6;
    rules[1].max_per_second = 10;
    config.set_span_sampler(span_sampler);

    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->span_sampler.rules.size() == 2);
    REQUIRE(finalized->span_sampler.rules[0].sample_rate == 0.5);
    REQUIRE(!finalized->span_sampler.rules[0].max_per_second);
    REQUIRE(finalized->span_sampler.rules[1].sample_rate == 0.6);
    REQUIRE(finalized->span_sampler.rules[1].max_per_second == 10);
  }

  SECTION("DD_SPAN_SAMPLING_RULES") {
    SECTION(
        "sets the span sampling rules, and overrides "
        "TracerConfig::span_sampler.rules") {
      SpanSamplerConfig::Rule config_rule;
      config_rule.service = "foosvc";
      config_rule.max_per_second = 9.2;

      SpanSamplerConfig span_sampler;
      span_sampler.rules.push_back(config_rule);

      config.set_span_sampler(span_sampler);

      auto rules_json = R"json([
        {"name": "mysql2.query", "max_per_second": 100},
        {"max_per_second": 10, "sample_rate": 0.1}
      ])json";

      const EnvGuard guard{"DD_SPAN_SAMPLING_RULES", rules_json};
      auto finalized = config.finalize();
      REQUIRE(finalized);
      const auto& rules = finalized->span_sampler.rules;
      REQUIRE(rules.size() == 2);
      REQUIRE(rules[0].service == "*");
      REQUIRE(rules[0].name == "mysql2.query");
      REQUIRE(rules[0].resource == "*");
      REQUIRE(rules[0].sample_rate == 1.0);
      REQUIRE(rules[0].max_per_second == 100);
      REQUIRE(rules[1].service == "*");
      REQUIRE(rules[1].name == "*");
      REQUIRE(rules[1].resource == "*");
      REQUIRE(rules[1].max_per_second == 10);
      REQUIRE(rules[1].sample_rate == 0.1);
    }

    SECTION("must be valid") {
      struct TestCase {
        std::string name;
        std::string json;
        Error::Code expected_error;
      };

      auto test_case = GENERATE(values<TestCase>({
          {"invalid JSON", "this is clearly not JSON",
           Error::SPAN_SAMPLING_RULES_INVALID_JSON},
          {"barely not JSON", "[true,]",
           Error::SPAN_SAMPLING_RULES_INVALID_JSON},
          {"must be array",
           R"json({"service": "you forgot the square brackets"})json",
           Error::SPAN_SAMPLING_RULES_WRONG_TYPE},
          {"service must be a string", R"json([{"service": 123}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"name must be a string", R"json([{"name": null}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"resource must be a string", R"json([{"resource": false}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"'tags' property must be an object",
           R"json([{"tags": ["foo:bar"]}])json",
           Error::RULE_PROPERTY_WRONG_TYPE},
          {"tag values must be strings",
           R"json([{"tags": {"foo": "two", "error": false}}])json",
           Error::RULE_TAG_WRONG_TYPE},
          {"each rule must be an object", R"json([["service", "wrong!"]])json",
           Error::RULE_WRONG_TYPE},
          {"sample_rate must be a number", R"json([{"sample_rate": true}])json",
           Error::SPAN_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE},
          {"max_per_second must be a number (or absent)",
           R"json([{"max_per_second": false}])json",
           Error::SPAN_SAMPLING_RULES_MAX_PER_SECOND_WRONG_TYPE},
          {"no unknown properties", R"json([{"extension": "denied!"}])json",
           Error::SPAN_SAMPLING_RULES_UNKNOWN_PROPERTY},
      }));

      CAPTURE(test_case.name);

      const EnvGuard guard{"DD_SPAN_SAMPLING_RULES", test_case.json};
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == test_case.expected_error);
    }

    SECTION("DD_SPAN_SAMPLING_RULES_FILE") {
      SECTION("successful usage") {
        const auto logger = std::make_shared<MockLogger>();
        config.set_logger(logger);

        // This rule will be overridden.
        SpanSamplerConfig::Rule config_rule;
        config_rule.service = "foosvc";
        config_rule.max_per_second = 9.2;

        SpanSamplerConfig span_sampler;
        span_sampler.rules.push_back(config_rule);

        config.set_span_sampler(span_sampler);

        auto rules_file_json = R"json([
        {"name": "mysql2.query"},
        {"resource": "/admin*"},
        {"max_per_second": 10, "sample_rate": 0.1}
      ])json";

        SomewhatSecureTemporaryFile file;
        REQUIRE(file.is_open());
        file << rules_file_json;
        file.close();
        const EnvGuard guard{"DD_SPAN_SAMPLING_RULES_FILE",
                             file.path().string()};

        SECTION("overrides SpanSamplerConfig::rules") {
          auto finalized = config.finalize();
          REQUIRE(finalized);
          const auto& rules = finalized->span_sampler.rules;
          REQUIRE(rules.size() == 3);
          REQUIRE(rules[0].name == "mysql2.query");
          REQUIRE(rules[1].resource == "/admin*");
          REQUIRE(rules[2].max_per_second == 10);
          REQUIRE(rules[2].sample_rate == 0.1);
        }

        SECTION("doesn't override DD_SPAN_SAMPLING_RULES, but logs an error") {
          auto rules_json = R"json([
            {"name": "mysql2.query", "max_per_second": 100},
            {"max_per_second": 10, "sample_rate": 0.1}
          ])json";

          const EnvGuard guard{"DD_SPAN_SAMPLING_RULES", rules_json};
          auto finalized = config.finalize();
          REQUIRE(finalized);
          const auto& rules = finalized->span_sampler.rules;
          REQUIRE(rules.size() == 2);
          REQUIRE(rules[0].name == "mysql2.query");
          REQUIRE(rules[0].max_per_second == 100);
          REQUIRE(rules[1].max_per_second == 10);
          REQUIRE(rules[1].sample_rate == 0.1);

          REQUIRE(logger->error_count() == 1);
        }
      }

      SECTION("failed usage") {
        SECTION("unable to open") {
          std::filesystem::path defunct;
          {
            SomewhatSecureTemporaryFile file;
            REQUIRE(file.is_open());
            defunct = file.path();
          }
          const EnvGuard guard{"DD_SPAN_SAMPLING_RULES_FILE", defunct.string()};
          auto finalized = config.finalize();
          REQUIRE(!finalized);
          REQUIRE(finalized.error().code == Error::SPAN_SAMPLING_RULES_FILE_IO);
        }

        SECTION("unable to parse") {
          SomewhatSecureTemporaryFile file;
          REQUIRE(file.is_open());
          // We could do any of the failures tested in the "must be valid"
          // section, since it's the same parser. Instead, just to cover the
          // code path specific to DD_SPAN_SAMPLING_RULES_FILE, pick any error,
          // e.g. invalid JSON.
          file << "this is clearly not JSON";
          file.close();
          const EnvGuard guard{"DD_SPAN_SAMPLING_RULES_FILE",
                               file.path().string()};
          auto finalized = config.finalize();
          REQUIRE(!finalized);
          REQUIRE(finalized.error().code ==
                  Error::SPAN_SAMPLING_RULES_INVALID_JSON);
        }
      }
    }
  }
}

TEST_CASE("TracerConfig propagation styles") {
  TracerConfig config;
  config.set_service_name("testsvc");

  SECTION("default style is [Datadog, W3C]") {
    auto finalized = config.finalize();
    REQUIRE(finalized);

    const std::vector<PropagationStyle> expected_styles = {
        PropagationStyle::DATADOG, PropagationStyle::W3C};

    REQUIRE(finalized->injection_styles == expected_styles);
    REQUIRE(finalized->extraction_styles == expected_styles);
  }

  SECTION("DD_TRACE_PROPAGATION_STYLE overrides defaults") {
    const EnvGuard guard{"DD_TRACE_PROPAGATION_STYLE", "B3"};
    auto finalized = config.finalize();
    REQUIRE(finalized);

    const std::vector<PropagationStyle> expected_styles = {
        PropagationStyle::B3};

    REQUIRE(finalized->injection_styles == expected_styles);
    REQUIRE(finalized->extraction_styles == expected_styles);
  }

  SECTION("injection_styles") {
    SECTION("need at least one") {
      config.set_injection_styles({});
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::MISSING_SPAN_INJECTION_STYLE);
    }

    SECTION("DD_TRACE_PROPAGATION_STYLE_INJECT") {
      SECTION("overrides injection_styles") {
        const EnvGuard guard{"DD_TRACE_PROPAGATION_STYLE_INJECT", "B3"};
        auto finalized = config.finalize();
        REQUIRE(finalized);
        const std::vector<PropagationStyle> expected_styles = {
            PropagationStyle::B3};
        REQUIRE(finalized->injection_styles == expected_styles);
      }

      SECTION("overrides DD_PROPAGATION_STYLE_INJECT") {
        const EnvGuard guard1{"DD_TRACE_PROPAGATION_STYLE_INJECT", "B3"};
        const EnvGuard guard2{"DD_PROPAGATION_STYLE_INJECT", "Datadog"};
        config.set_logger(std::make_shared<MockLogger>());  // suppress warning
        auto finalized = config.finalize();
        REQUIRE(finalized);
        const std::vector<PropagationStyle> expected_styles = {
            PropagationStyle::B3};
        REQUIRE(finalized->injection_styles == expected_styles);
      }

      SECTION("overrides DD_TRACE_PROPAGATION_STYLE") {
        const EnvGuard guard1{"DD_TRACE_PROPAGATION_STYLE_INJECT", "B3"};
        const EnvGuard guard2{"DD_TRACE_PROPAGATION_STYLE", "Datadog"};
        config.set_logger(std::make_shared<MockLogger>());  // suppress warning
        auto finalized = config.finalize();
        REQUIRE(finalized);
        const std::vector<PropagationStyle> expected_styles = {
            PropagationStyle::B3};
        REQUIRE(finalized->injection_styles == expected_styles);
      }

      SECTION("parsing") {
        struct TestCase {
          int line;
          std::string env_value;
          Optional<Error::Code> expected_error;
          std::vector<PropagationStyle> expected_styles = {};
        };

        // brevity
        const auto datadog = PropagationStyle::DATADOG,
                   b3 = PropagationStyle::B3, none = PropagationStyle::NONE;
        // clang-format off
        auto test_case = GENERATE(values<TestCase>({
          {__LINE__, "Datadog", x, {datadog}},
          {__LINE__, "DaTaDoG", x, {datadog}},
          {__LINE__, "B3", x, {b3}},
          {__LINE__, "b3", x, {b3}},
          {__LINE__, "b3MULTI", x, {b3}},
          {__LINE__, "b3, b3multi", Error::DUPLICATE_PROPAGATION_STYLE  },
          {__LINE__, "Datadog B3", x, {datadog, b3}},
          {__LINE__, "Datadog B3 none", x, {datadog, b3, none}},
          {__LINE__, "NONE", x, {none}},
          {__LINE__, "B3 Datadog", x, {b3, datadog}},
          {__LINE__, "b3 datadog", x, {b3, datadog}},
          {__LINE__, "b3, datadog", x, {b3, datadog}},
          {__LINE__, "b3,datadog", x, {b3, datadog}},
          {__LINE__, "b3,             datadog", x, {b3, datadog}},
          {__LINE__, "b3,,datadog", Error::UNKNOWN_PROPAGATION_STYLE},
          {__LINE__, "b3,datadog,w3c", Error::UNKNOWN_PROPAGATION_STYLE},
          {__LINE__, "b3,datadog,datadog", Error::DUPLICATE_PROPAGATION_STYLE},
          {__LINE__, "  b3 b3 b3, b3 , b3, b3, b3   , b3 b3 b3  ", Error::DUPLICATE_PROPAGATION_STYLE},
        }));
        // clang-format on

        CAPTURE(test_case.line);
        CAPTURE(test_case.env_value);

        const EnvGuard guard{"DD_TRACE_PROPAGATION_STYLE_INJECT",
                             test_case.env_value};
        auto finalized = config.finalize();
        if (test_case.expected_error) {
          REQUIRE(!finalized);
          REQUIRE(finalized.error().code == *test_case.expected_error);
        } else {
          REQUIRE(finalized);
          REQUIRE(finalized->injection_styles == test_case.expected_styles);
        }
      }
    }
  }

  // This section is very much like "injection_styles", above.
  SECTION("extraction_styles") {
    SECTION("need at least one") {
      config.set_extraction_styles({});
      auto finalized = config.finalize();
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::MISSING_SPAN_EXTRACTION_STYLE);
    }

    SECTION("DD_TRACE_PROPAGATION_STYLE_EXTRACT") {
      SECTION("overrides extraction_styles") {
        const EnvGuard guard{"DD_TRACE_PROPAGATION_STYLE_EXTRACT", "B3"};
        auto finalized = config.finalize();
        REQUIRE(finalized);
        const std::vector<PropagationStyle> expected_styles = {
            PropagationStyle::B3};
        REQUIRE(finalized->extraction_styles == expected_styles);
      }

      SECTION("overrides DD_PROPAGATION_STYLE_EXTRACT") {
        const EnvGuard guard1{"DD_TRACE_PROPAGATION_STYLE_EXTRACT", "B3"};
        const EnvGuard guard2{"DD_PROPAGATION_STYLE_EXTRACT", "Datadog"};
        config.set_logger(std::make_shared<MockLogger>());  // suppress warning
        auto finalized = config.finalize();
        REQUIRE(finalized);
        const std::vector<PropagationStyle> expected_styles = {
            PropagationStyle::B3};
        REQUIRE(finalized->extraction_styles == expected_styles);
      }

      SECTION("overrides DD_TRACE_PROPAGATION_STYLE") {
        const EnvGuard guard1{"DD_TRACE_PROPAGATION_STYLE_EXTRACT", "B3"};
        const EnvGuard guard2{"DD_TRACE_PROPAGATION_STYLE", "Datadog"};
        config.set_logger(std::make_shared<MockLogger>());  // suppress warning
        auto finalized = config.finalize();
        REQUIRE(finalized);
        const std::vector<PropagationStyle> expected_styles = {
            PropagationStyle::B3};
        REQUIRE(finalized->extraction_styles == expected_styles);
      }

      // It's the same as for injection styles, so let's omit most of the
      // section.  Keep only an example where parsing fails, so we cover the
      // error handling code in `TracerConfig`.
      SECTION("parsing failure") {
        const EnvGuard guard{"DD_PROPAGATION_STYLE_EXTRACT", "b3,,datadog"};
        auto finalized = config.finalize();
        REQUIRE(!finalized);
        REQUIRE(finalized.error().code == Error::UNKNOWN_PROPAGATION_STYLE);
      }
    }
  }

  SECTION("warn if one env var overrides another") {
    const auto logger = std::make_shared<MockLogger>();
    config.set_logger(logger);
    const auto ts = "DD_TRACE_PROPAGATION_STYLE";
    const auto tse = "DD_TRACE_PROPAGATION_STYLE_EXTRACT";
    const auto se = "DD_PROPAGATION_STYLE_EXTRACT";
    const auto tsi = "DD_TRACE_PROPAGATION_STYLE_INJECT";
    const auto si = "DD_PROPAGATION_STYLE_INJECT";
    const char* const vars[] = {ts, tse, se, tsi, si};
    constexpr auto n = sizeof(vars) / sizeof(vars[0]);
    // clang-format off
    const bool x = false; // ignored values
    const bool expect_warning[n][n] = {
    //          ts    tse   se    tsi    si
    //          ---   ---   ---   ---    ---
    /* ts  */{  x,    true, true, true,  true  },

    /* tse */{  x,    x,    true, false, false },

    /* se  */{  x,    x,    x,    false, false },

    /* tsi */{  x,    x,    x,    x,     true  },

    /* si  */{  x,    x,    x,    x,     x     },
    };
    // clang-format on
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t j = i + 1; j < n; ++j) {
        CAPTURE(i);
        CAPTURE(vars[i]);
        CAPTURE(j);
        CAPTURE(vars[j]);
        CAPTURE(expect_warning[i][j]);
        const EnvGuard guard1{vars[i], "B3"};
        const EnvGuard guard2{vars[j], "B3"};
        const auto finalized_config = config.finalize();
        REQUIRE(finalized_config);
        if (expect_warning[i][j]) {
          REQUIRE(logger->error_count() == 1);
          REQUIRE(logger->first_error().code ==
                  Error::MULTIPLE_PROPAGATION_STYLE_ENVIRONMENT_VARIABLES);
        } else {
          REQUIRE(logger->error_count() == 0);
        }
        logger->entries.clear();
      }
    }
  }
}

TEST_CASE("configure 128-bit trace IDs") {
  TracerConfig config;
  config.set_service_name("testsvc");

  SECTION("defaults to true") {
    const auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_id_128_bit == true);
  }

  SECTION("value honored in finalizer") {
    const auto value = GENERATE(true, false);
    config.enable_128bit_trace_id(value);
    const auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_id_128_bit == value);
  }

  SECTION("value overridden by DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED") {
    struct TestCase {
      int line;
      std::string env_value;
      bool expected_value;
    };

    // clang-format off
    const auto test_case = GENERATE(values<TestCase>({
      {__LINE__, "true", true},
      {__LINE__, "false", false},
      {__LINE__, "no", false},
      {__LINE__, "nein", true},
      {__LINE__, "0", false},
      {__LINE__, "", true},
    }));
    // clang-format on

    CAPTURE(test_case.line);
    CAPTURE(test_case.env_value);

    EnvGuard guard{"DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED",
                   test_case.env_value};

    config.enable_128bit_trace_id(true);
    auto finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_id_128_bit == test_case.expected_value);

    config.enable_128bit_trace_id(false);
    // CAPTURE(config.trace_id_128_bit);
    finalized = config.finalize();
    REQUIRE(finalized);
    REQUIRE(finalized->trace_id_128_bit == test_case.expected_value);
  }
}
