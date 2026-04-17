#include <datadog/c/tracer.h>
#include <datadog/tracer.h>

#include "mocks/collectors.h"
#include "null_logger.h"
#include "test.h"

namespace dd = datadog::tracing;

namespace {

constexpr size_t trace_id_buf_size = 33;
constexpr size_t span_id_buf_size = 17;

std::unordered_map<std::string, std::string> g_headers;

const char *test_header_reader(const char *key) {
  auto it = g_headers.find(key);
  if (it != g_headers.end()) {
    return it->second.c_str();
  }
  return nullptr;
}

void test_header_writer(const char *key, const char *value) {
  g_headers[key] = value;
}

struct TestTracer {
  std::shared_ptr<MockCollector> collector;
  dd_tracer_t *tracer;

  ~TestTracer() { dd_tracer_free(tracer); }
};

TestTracer make_tracer() {
  auto *conf = dd_tracer_conf_new();
  dd_tracer_conf_set(conf, DD_OPT_SERVICE_NAME, "test-service");

  // Inject mocks before const handoff to dd_tracer_new.
  auto *cfg = reinterpret_cast<dd::TracerConfig *>(conf);
  TestTracer result;
  result.collector = std::make_shared<MockCollector>();
  cfg->collector = result.collector;
  cfg->logger = std::make_shared<dd::NullLogger>();

  result.tracer = dd_tracer_new(conf, nullptr);
  dd_tracer_conf_free(conf);
  REQUIRE(result.tracer != nullptr);
  return result;
}

}  // namespace

TEST_CASE("tracer lifecycle", "[c_binding]") {
  auto *conf = dd_tracer_conf_new();
  REQUIRE(conf != nullptr);

  dd_tracer_conf_set(conf, DD_OPT_SERVICE_NAME, "my-service");
  dd_tracer_conf_set(conf, DD_OPT_ENV, "staging");
  dd_tracer_conf_set(conf, DD_OPT_VERSION, "1.0.0");
  dd_tracer_conf_set(conf, DD_OPT_AGENT_URL, "http://foo:8080");
  dd_tracer_conf_set(conf, DD_OPT_INTEGRATION_NAME, "my-integration");
  dd_tracer_conf_set(conf, DD_OPT_INTEGRATION_VERSION, "2.0.0");

  // Inject mocks so dd_tracer_new succeeds without a real agent.
  auto *cfg = reinterpret_cast<dd::TracerConfig *>(conf);
  cfg->collector = std::make_shared<MockCollector>();
  cfg->logger = std::make_shared<dd::NullLogger>();

  auto *tracer = dd_tracer_new(conf, nullptr);
  REQUIRE(tracer != nullptr);

  dd_tracer_conf_free(conf);
  dd_tracer_free(tracer);
}

TEST_CASE("tracer new propagates error", "[c_binding]") {
  auto *conf = dd_tracer_conf_new();
  dd_tracer_conf_set(conf, DD_OPT_AGENT_URL, "not://valid");

  dd_error_t err = {};
  auto *tracer = dd_tracer_new(conf, &err);
  CHECK(tracer == nullptr);
  CHECK(err.code == DD_ERROR_INVALID_CONFIG);
  CHECK(err.message[0] != '\0');

  dd_tracer_conf_free(conf);
}

TEST_CASE("span create, tag, finish, free", "[c_binding]") {
  auto ctx = make_tracer();

  const int64_t start_ns = 1'700'000'000'000'000'000LL;
  const int64_t end_ns = start_ns + 42'000'000LL;

  auto *span = dd_tracer_create_span(
      ctx.tracer, {.name = "test.op", .start_time_ns = start_ns});
  REQUIRE(span != nullptr);

  dd_span_set_tag(span, "http.method", "GET");
  dd_span_set_resource(span, "GET /api/users");
  dd_span_set_service(span, "user-service");
  dd_span_set_error(span, 1);
  dd_span_set_error_message(span, "something broke");

  dd_span_finish_with_time(span, end_ns);
  dd_span_free(span);

  const auto &sd = ctx.collector->first_span();
  CHECK(sd.tags.at("http.method") == "GET");
  CHECK(sd.resource == "GET /api/users");
  CHECK(sd.service == "user-service");
  CHECK(sd.error == true);
  CHECK(sd.tags.at("error.message") == "something broke");

  CHECK(std::chrono::duration_cast<std::chrono::nanoseconds>(
            sd.start.wall.time_since_epoch())
            .count() == start_ns);
  CHECK(std::chrono::abs(sd.duration -
                         std::chrono::nanoseconds(end_ns - start_ns)) <
        std::chrono::milliseconds(1));
}

TEST_CASE("finish clamps negative durations to zero", "[c_binding]") {
  auto ctx = make_tracer();

  // Explicit end before explicit start.
  const int64_t past_ns = 1'700'000'000'000'000'000LL;
  auto *s1 = dd_tracer_create_span(
      ctx.tracer, {.name = "explicit", .start_time_ns = past_ns});
  dd_span_finish_with_time(s1, past_ns - 1'000'000LL);
  dd_span_free(s1);

  // Sentinel finish (steady_clock::now()) with a start projected into
  // the future — exercises the implicit-path clamp.
  const int64_t future_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          (std::chrono::system_clock::now() + std::chrono::hours(1))
              .time_since_epoch())
          .count();
  auto *s2 = dd_tracer_create_span(
      ctx.tracer, {.name = "implicit", .start_time_ns = future_ns});
  dd_span_finish(s2);
  dd_span_free(s2);

  for (const auto &chunk : ctx.collector->chunks)
    for (const auto &span : chunk)
      CHECK(span->duration >= std::chrono::nanoseconds(0));
}

TEST_CASE("create span with resource", "[c_binding]") {
  auto ctx = make_tracer();

  auto *span = dd_tracer_create_span(
      ctx.tracer, {.name = "web.request", .resource = "GET /api/users"});
  REQUIRE(span != nullptr);

  dd_span_finish(span);
  dd_span_free(span);

  const auto &sd = ctx.collector->first_span();
  CHECK(sd.resource == "GET /api/users");
}

TEST_CASE("span free without finish auto-finishes", "[c_binding]") {
  auto ctx = make_tracer();

  auto *span = dd_tracer_create_span(ctx.tracer, {.name = "auto.finish"});
  REQUIRE(span != nullptr);

  dd_span_set_tag(span, "key", "value");

  // Free without calling dd_span_finish — should auto-finish.
  dd_span_free(span);

  const auto &sd = ctx.collector->first_span();
  CHECK(sd.name == "auto.finish");
  CHECK(sd.tags.at("key") == "value");
}

TEST_CASE("inject then extract preserves trace ID", "[c_binding]") {
  auto ctx = make_tracer();

  auto *span_1 = dd_tracer_create_span(ctx.tracer, {.name = "producer"});
  g_headers.clear();
  dd_span_inject(span_1, test_header_writer);
  CHECK(!g_headers.empty());

  char trace_id_1[trace_id_buf_size] = {};
  dd_span_get_trace_id(span_1, trace_id_1, sizeof(trace_id_1));

  auto *span_2 = dd_tracer_extract_or_create_span(
      ctx.tracer, test_header_reader,
      {.name = "consumer", .resource = "GET /downstream"});
  REQUIRE(span_2 != nullptr);

  char trace_id_2[trace_id_buf_size] = {};
  dd_span_get_trace_id(span_2, trace_id_2, sizeof(trace_id_2));

  CHECK(std::string(trace_id_1) == std::string(trace_id_2));

  dd_span_finish(span_1);
  dd_span_free(span_1);
  dd_span_finish(span_2);
  dd_span_free(span_2);
}

TEST_CASE("child span shares trace ID", "[c_binding]") {
  auto ctx = make_tracer();

  auto *parent = dd_tracer_create_span(ctx.tracer, {.name = "parent.op"});
  REQUIRE(parent != nullptr);

  auto *child = dd_span_create_child(parent, {.name = "child.op"});
  REQUIRE(child != nullptr);

  char parent_trace[trace_id_buf_size] = {};
  char child_trace[trace_id_buf_size] = {};
  dd_span_get_trace_id(parent, parent_trace, sizeof(parent_trace));
  dd_span_get_trace_id(child, child_trace, sizeof(child_trace));
  CHECK(std::string(parent_trace) == std::string(child_trace));

  char parent_span_id[span_id_buf_size] = {};
  char child_span_id[span_id_buf_size] = {};
  dd_span_get_span_id(parent, parent_span_id, sizeof(parent_span_id));
  dd_span_get_span_id(child, child_span_id, sizeof(child_span_id));
  CHECK(std::string(parent_span_id) != std::string(child_span_id));

  dd_span_finish(child);
  dd_span_free(child);
  dd_span_finish(parent);
  dd_span_free(parent);
}

TEST_CASE("child span with all options", "[c_binding]") {
  auto ctx = make_tracer();

  auto *parent = dd_tracer_create_span(ctx.tracer, {.name = "parent.op"});
  REQUIRE(parent != nullptr);

  auto *child = dd_span_create_child(parent, {.name = "db.query",
                                              .resource = "SELECT *",
                                              .service = "postgres",
                                              .service_type = "sql",
                                              .environment = "staging",
                                              .version = "2.0"});
  REQUIRE(child != nullptr);

  dd_span_finish(child);
  dd_span_free(child);
  dd_span_finish(parent);
  dd_span_free(parent);

  // Spans are sent as a chunk in registration order:
  // parent first, child second.
  REQUIRE(ctx.collector->chunks.size() >= 1);
  REQUIRE(ctx.collector->chunks[0].size() >= 2);
  const auto &child_sd = *ctx.collector->chunks[0][1];
  CHECK(child_sd.name == "db.query");
  CHECK(child_sd.resource == "SELECT *");
  CHECK(child_sd.service == "postgres");
  CHECK(child_sd.service_type == "sql");
}

TEST_CASE("tracer new with invalid config and null error", "[c_binding]") {
  auto *conf = dd_tracer_conf_new();
  dd_tracer_conf_set(conf, DD_OPT_AGENT_URL, "not://valid");

  auto *tracer = dd_tracer_new(conf, nullptr);
  CHECK(tracer == nullptr);

  dd_tracer_conf_free(conf);
}

TEST_CASE("null arguments do not crash", "[c_binding]") {
  CHECK(dd_tracer_new(nullptr, nullptr) == nullptr);
  CHECK(dd_tracer_create_span(nullptr, {.name = "x"}) == nullptr);
  CHECK(dd_span_create_child(nullptr, {.name = "x"}) == nullptr);

  char buf[trace_id_buf_size] = {};
  CHECK(dd_span_get_trace_id(nullptr, buf, sizeof(buf)) == -1);
  CHECK(dd_span_get_span_id(nullptr, buf, sizeof(buf)) == -1);

  dd_tracer_conf_free(nullptr);
  dd_tracer_conf_set(nullptr, DD_OPT_SERVICE_NAME, "x");
  dd_tracer_free(nullptr);
  dd_span_free(nullptr);
  dd_span_set_tag(nullptr, "k", "v");
  dd_span_set_error(nullptr, 1);
  dd_span_set_error_message(nullptr, "msg");
  dd_span_inject(nullptr, test_header_writer);
  dd_span_finish(nullptr);
  dd_span_finish_with_time(nullptr, 0);
  dd_span_set_resource(nullptr, "res");
  dd_span_set_service(nullptr, "svc");
}
