#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <cstdlib>
#include <optional>
#include <string>

#include "collectors.h"
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

TEST_CASE("log_on_startup") {
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

TEST_CASE("report_traces") {
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

// TODO: samplers and propagation styles
