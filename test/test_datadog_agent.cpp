#include <datadog/collector_response.h>
#include <datadog/datadog_agent_config.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <iostream>

#include "mocks/event_schedulers.h"
#include "mocks/http_clients.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;

TEST_CASE("CollectorResponse") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  const auto logger =
      std::make_shared<MockLogger>(std::cerr, MockLogger::ERRORS_ONLY);
  const auto event_scheduler = std::make_shared<MockEventScheduler>();
  const auto http_client = std::make_shared<MockHTTPClient>();
  config.logger = logger;
  config.agent.event_scheduler = event_scheduler;
  config.agent.http_client = http_client;
  auto finalized = finalize_config(config);
  REQUIRE(finalized);

  SECTION("empty object is valid") {
    {
      http_client->response_status = 200;
      http_client->response_body << "{}";
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 0);
  }

  SECTION("just the default key") {
    {
      http_client->response_status = 200;
      http_client->response_body << "{\"rate_by_service\": {\""
                                 << CollectorResponse::key_of_default_rate
                                 << "\": 1.0}}";
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 0);
  }

  SECTION("default key and another key") {
    {
      http_client->response_status = 200;
      http_client->response_body
          << "{\"rate_by_service\": {\""
          << CollectorResponse::key_of_default_rate
          << "\": 1.0, \"service:wiggle,env:foo\": 0.0}}";
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 0);
  }

  SECTION("invalid responses") {
    // Don't echo error messages.
    logger->echo = nullptr;

    struct TestCase {
      std::string name;
      std::string response_body;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"not JSON", "well that's not right at all!"},
        {"not an object", "[\"wrong\", \"type\", 123]"},
        {"rate_by_service not an object", "{\"rate_by_service\": null}"},
        {"sample rate not a number",
         "{\"rate_by_service\": {\"service:foo,env:bar\": []}}"},
        {"invalid sample rate",
         "{\"rate_by_service\": {\"service:foo,env:bar\": -1.337}}"},
    }));

    CAPTURE(test_case.name);
    {
      http_client->response_status = 200;
      http_client->response_body << test_case.response_body;
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 1);
  }

  SECTION("HTTP non-success response code") {
    // Don't echo error messages.
    logger->echo = nullptr;

    auto status = GENERATE(range(300, 600));
    {
      http_client->response_status = status;
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 1);
  }

  SECTION("HTTP client failure") {
    // Don't echo error messages.
    logger->echo = nullptr;

    const Error error{Error::OTHER, "oh no!"};
    {
      http_client->response_error = error;
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 1);
    REQUIRE(logger->first_error().code == error.code);
  }

  SECTION("HTTPClient post() failure") {
    // Don't echo error messages.
    logger->echo = nullptr;

    const Error error{Error::OTHER, "oh no!"};
    {
      http_client->post_error = error;
      Tracer tracer{*finalized};
      auto span = tracer.create_span();
      (void)span;
    }
    REQUIRE(event_scheduler->cancelled);
    REQUIRE(logger->error_count() == 1);
    REQUIRE(logger->first_error().code == error.code);
  }
}
