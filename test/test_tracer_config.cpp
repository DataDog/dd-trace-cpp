#include <datadog/threaded_event_scheduler.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <cstdlib>
#include <optional>
#include <string>

#include "collectors.h"
#include "event_schedulers.h"
#include "loggers.h"
#include "test.h"
#ifdef _MSC_VER
#include <winbase.h>  // SetEnvironmentVariable
#else
#include <stdlib.h>  // setenv, unsetenv
#endif

using namespace datadog::tracing;

namespace {

class EnvGuard {
  std::string name_;
  std::optional<std::string> former_value_;

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
// `std::optional<...>` data members.
const auto x = std::nullopt;

}  // namespace

TEST_CASE("TracerConfig::defaults") {
  TracerConfig config;

  SECTION("service is required") {
    SECTION("empty") {
      auto finalized = finalize_config(config);
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code == Error::SERVICE_NAME_REQUIRED);
    }
    SECTION("nonempty") {
      config.defaults.service = "testsvc";
      auto finalized = finalize_config(config);
      REQUIRE(finalized);
    }
  }

  SECTION("DD_SERVICE overrides service") {
    const EnvGuard guard{"DD_SERVICE", "foosvc"};
    config.defaults.service = "testsvc";
    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    REQUIRE(finalized->defaults.service == "foosvc");
  }

  SECTION("DD_ENV overrides environment") {
    const EnvGuard guard{"DD_ENV", "prod"};
    config.defaults.environment = "dev";
    config.defaults.service = "required";
    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    REQUIRE(finalized->defaults.environment == "prod");
  }

  SECTION("DD_VERSION overrides version") {
    const EnvGuard guard{"DD_VERSION", "v2"};
    config.defaults.version = "v1";
    config.defaults.service = "required";
    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    REQUIRE(finalized->defaults.version == "v2");
  }

  SECTION("DD_TAGS") {
    struct TestCase {
      std::string name;
      std::string dd_tags;
      std::unordered_map<std::string, std::string> expected_tags;
      std::optional<Error::Code> expected_error;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"empty", "", {}, std::nullopt},
        {"missing colon", "foo", {}, Error::TAG_MISSING_SEPARATOR},
        {"trailing comma",
         "foo:bar, baz:123,",
         {},
         Error::TAG_MISSING_SEPARATOR},
        {"overwrite value", "foo:baz", {{"foo", "baz"}}, std::nullopt},
        {"additional values",
         "baz:123, bam:three",
         {{"baz", "123"}, {"bam", "three"}},
         std::nullopt},
        {"commas optional",
         "baz:123 bam:three",
         {{"baz", "123"}, {"bam", "three"}},
         std::nullopt},
        {"last one wins",
         "baz:123 baz:three",
         {{"baz", "three"}},
         std::nullopt},
    }));

    // This will be overriden by the DD_TAGS environment variable.
    config.defaults.tags = {{"foo", "bar"}};
    config.defaults.service = "required";

    CAPTURE(test_case.name);
    const EnvGuard guard{"DD_TAGS", test_case.dd_tags};
    auto finalized = finalize_config(config);
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
  config.defaults.service = "testsvc";
  const auto logger = std::make_shared<MockLogger>();
  config.logger = logger;

  SECTION("default is true") {
    {
      auto finalized = finalize_config(config);
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
      config.log_on_startup = false;
      auto finalized = finalize_config(config);
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
      auto finalized = finalize_config(config);
      REQUIRE(finalized);
      const Tracer tracer{*finalized};
      (void)tracer;
    }
    REQUIRE(logger->startup_count() == int(test_case.expect_startup_log));
  }
}

TEST_CASE("TracerConfig::report_traces") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  const auto collector = std::make_shared<MockCollector>();
  config.collector = collector;
  config.logger = std::make_shared<NullLogger>();

  SECTION("default is true") {
    {
      auto finalized = finalize_config(config);
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
      config.report_traces = false;
      auto finalized = finalize_config(config);
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
    config.report_traces = test_case.original_value;
    {
      auto finalized = finalize_config(config);
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
  config.defaults.service = "testsvc";

  SECTION("event_scheduler") {
    SECTION("default") {
      auto finalized = finalize_config(config);
      REQUIRE(finalized);
      const auto* const agent =
          std::get_if<FinalizedDatadogAgentConfig>(&finalized->collector);
      REQUIRE(agent);
      REQUIRE(
          dynamic_cast<ThreadedEventScheduler*>(agent->event_scheduler.get()));
    }

    SECTION("custom") {
      auto scheduler = std::make_shared<MockEventScheduler>();
      config.agent.event_scheduler = scheduler;
      auto finalized = finalize_config(config);
      REQUIRE(finalized);
      const auto* const agent =
          std::get_if<FinalizedDatadogAgentConfig>(&finalized->collector);
      REQUIRE(agent);
      REQUIRE(agent->event_scheduler == scheduler);
    }
  }

  SECTION("flush interval") {
    SECTION("cannot be zero") {
      config.agent.flush_interval_milliseconds = 0;
      auto finalized = finalize_config(config);
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code ==
              Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL);
    }

    SECTION("cannot be negative") {
      config.agent.flush_interval_milliseconds = -1337;
      auto finalized = finalize_config(config);
      REQUIRE(!finalized);
      REQUIRE(finalized.error().code ==
              Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL);
    }
  }

  SECTION("url") {
    SECTION("parsing") {
      struct TestCase {
        std::string url;
        std::optional<Error::Code> expected_error;
        std::string expected_scheme = "";
        std::string expected_authority = "";
        std::string expected_path = "";
      };

      auto test_case = GENERATE(values<TestCase>({
          {"http://dd-agent:8126", std::nullopt, "http", "dd-agent:8126", ""},
          {"http://dd-agent:8126/", std::nullopt, "http", "dd-agent:8126", "/"},
          {"https://dd-agent:8126/", std::nullopt, "https", "dd-agent:8126",
           "/"},
          {"unix:///var/run/datadog/trace-agent.sock", std::nullopt, "unix",
           "/var/run/datadog/trace-agent.sock"},
          {"unix://var/run/datadog/trace-agent.sock",
           Error::URL_UNIX_DOMAIN_SOCKET_PATH_NOT_ABSOLUTE},
          {"http+unix:///run/datadog/trace-agent.sock", std::nullopt,
           "http+unix", "/run/datadog/trace-agent.sock"},
          {"https+unix:///run/datadog/trace-agent.sock", std::nullopt,
           "https+unix", "/run/datadog/trace-agent.sock"},
          {"tcp://localhost:8126", Error::URL_UNSUPPORTED_SCHEME},
          {"/var/run/datadog/trace-agent.sock", Error::URL_MISSING_SEPARATOR},
      }));

      CAPTURE(test_case.url);
      config.agent.url = test_case.url;
      auto finalized = finalize_config(config);
      if (test_case.expected_error) {
        REQUIRE(!finalized);
        REQUIRE(finalized.error().code == *test_case.expected_error);
      } else {
        REQUIRE(finalized);
        const auto* const agent =
            std::get_if<FinalizedDatadogAgentConfig>(&finalized->collector);
        REQUIRE(agent);
        REQUIRE(agent->url.scheme == test_case.expected_scheme);
        REQUIRE(agent->url.authority == test_case.expected_authority);
        REQUIRE(agent->url.path == test_case.expected_path);
      }
    }

    SECTION("environment variables override") {
      struct TestCase {
        std::string name;
        std::optional<std::string> env_host;
        std::optional<std::string> env_port;
        std::optional<std::string> env_url;
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
      std::optional<EnvGuard> host_guard;
      if (test_case.env_host) {
        host_guard.emplace("DD_AGENT_HOST", *test_case.env_host);
      }
      std::optional<EnvGuard> port_guard;
      if (test_case.env_port) {
        port_guard.emplace("DD_TRACE_AGENT_PORT", *test_case.env_port);
      }
      std::optional<EnvGuard> url_guard;
      if (test_case.env_url) {
        url_guard.emplace("DD_TRACE_AGENT_URL", *test_case.env_url);
      }

      auto finalized = finalize_config(config);
      REQUIRE(finalized);
      const auto* const agent =
          std::get_if<FinalizedDatadogAgentConfig>(&finalized->collector);
      REQUIRE(agent);
      REQUIRE(agent->url.scheme == test_case.expected_scheme);
      REQUIRE(agent->url.authority == test_case.expected_authority);
    }
  }
}

/*
TEST_CASE("TracerConfig::trace_sampler") {
  // TODO
}

TEST_CASE("TracerConfig::span_sampler") {
  // TODO
}
*/
