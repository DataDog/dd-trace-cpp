#define CATCH_CONFIG_MAIN

#include <datadog/c/tracer.h>
#include <datadog/tracer.h>

#include "mocks/collectors.h"
#include "null_logger.h"

namespace dd = datadog::tracing;

namespace {

constexpr size_t kTraceIdBufSize = 33;
constexpr size_t kSpanIdBufSize = 17;

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

} // namespace

struct CBindingFixture {
    std::shared_ptr<MockCollector> collector;
    ddog_trace_tracer_t *tracer;

    CBindingFixture() {
        auto *conf = ddog_trace_tracer_conf_new();
        ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_SERVICE_NAME,
                                   "test-service");

        auto *cfg = reinterpret_cast<dd::TracerConfig *>(conf);
        collector = std::make_shared<MockCollector>();
        cfg->collector = collector;
        cfg->logger = std::make_shared<dd::NullLogger>();

        tracer = ddog_trace_tracer_new(conf);
        ddog_trace_tracer_conf_free(conf);
        REQUIRE(tracer != nullptr);
    }

    ~CBindingFixture() { ddog_trace_tracer_free(tracer); }
};

TEST_CASE("tracer lifecycle", "[c_binding]") {
    auto *conf = ddog_trace_tracer_conf_new();
    REQUIRE(conf != nullptr);

    ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_SERVICE_NAME, "my-service");
    ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_ENV, "staging");
    ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_VERSION, "1.0.0");
    ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_AGENT_URL,
                               "http://localhost:8126");
    ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_INTEGRATION_NAME,
                               "my-integration");
    ddog_trace_tracer_conf_set(conf, DDOG_TRACE_OPT_INTEGRATION_VERSION,
                               "2.0.0");

    // Inject mocks so ddog_trace_tracer_new succeeds without a real agent.
    auto *cfg = reinterpret_cast<dd::TracerConfig *>(conf);
    auto collector = std::make_shared<MockCollector>();
    cfg->collector = collector;
    cfg->logger = std::make_shared<dd::NullLogger>();

    auto *tracer = ddog_trace_tracer_new(conf);
    REQUIRE(tracer != nullptr);

    ddog_trace_tracer_conf_free(conf);
    ddog_trace_tracer_free(tracer);
}

TEST_CASE_METHOD(CBindingFixture, "span create, tag, finish, free",
                 "[c_binding]") {
    auto *span = ddog_trace_tracer_create_span(tracer, "test.op");
    REQUIRE(span != nullptr);

    ddog_trace_span_set_tag(span, "http.method", "GET");
    ddog_trace_span_set_resource(span, "GET /api/users");
    ddog_trace_span_set_service(span, "user-service");
    ddog_trace_span_set_error(span, 1);
    ddog_trace_span_set_error_message(span, "something broke");

    const char *keys[] = {"batch.key1", "batch.key2"};
    const char *vals[] = {"val1", "val2"};
    ddog_trace_span_set_tags(span, keys, vals, 2);

    ddog_trace_span_finish(span);
    ddog_trace_span_free(span);
    ddog_trace_tracer_free(tracer);
    tracer = nullptr;

    const auto &sd = collector->first_span();
    CHECK(sd.tags.at("http.method") == "GET");
    CHECK(sd.resource == "GET /api/users");
    CHECK(sd.service == "user-service");
    CHECK(sd.error == true);
    CHECK(sd.tags.at("error.message") == "something broke");
    CHECK(sd.tags.at("batch.key1") == "val1");
    CHECK(sd.tags.at("batch.key2") == "val2");
}

TEST_CASE_METHOD(CBindingFixture, "inject then extract preserves trace ID",
                 "[c_binding]") {
    auto *span1 = ddog_trace_tracer_create_span(tracer, "producer");
    g_headers.clear();
    ddog_trace_span_inject(span1, test_header_writer);
    CHECK(!g_headers.empty());

    char trace_id_1[kTraceIdBufSize] = {};
    ddog_trace_span_get_trace_id(span1, trace_id_1, sizeof(trace_id_1));

    auto *span2 = ddog_trace_tracer_extract_or_create_span(
        tracer, test_header_reader, "consumer", "GET /downstream");
    REQUIRE(span2 != nullptr);

    char trace_id_2[kTraceIdBufSize] = {};
    ddog_trace_span_get_trace_id(span2, trace_id_2, sizeof(trace_id_2));

    CHECK(std::string(trace_id_1) == std::string(trace_id_2));

    ddog_trace_span_finish(span1);
    ddog_trace_span_free(span1);
    ddog_trace_span_finish(span2);
    ddog_trace_span_free(span2);
}

TEST_CASE_METHOD(CBindingFixture, "child span shares trace ID", "[c_binding]") {
    auto *parent = ddog_trace_tracer_create_span(tracer, "parent.op");
    REQUIRE(parent != nullptr);

    auto *child = ddog_trace_span_create_child(parent, "child.op");
    REQUIRE(child != nullptr);

    char parent_trace[kTraceIdBufSize] = {};
    char child_trace[kTraceIdBufSize] = {};
    ddog_trace_span_get_trace_id(parent, parent_trace, sizeof(parent_trace));
    ddog_trace_span_get_trace_id(child, child_trace, sizeof(child_trace));
    CHECK(std::string(parent_trace) == std::string(child_trace));

    char parent_span_id[kSpanIdBufSize] = {};
    char child_span_id[kSpanIdBufSize] = {};
    ddog_trace_span_get_span_id(parent, parent_span_id, sizeof(parent_span_id));
    ddog_trace_span_get_span_id(child, child_span_id, sizeof(child_span_id));
    CHECK(std::string(parent_span_id) != std::string(child_span_id));

    ddog_trace_span_finish(child);
    ddog_trace_span_free(child);
    ddog_trace_span_finish(parent);
    ddog_trace_span_free(parent);
}

TEST_CASE_METHOD(CBindingFixture, "sampling priority", "[c_binding]") {
    auto *span = ddog_trace_tracer_create_span(tracer, "test.op");
    REQUIRE(span != nullptr);

    ddog_trace_span_set_sampling_priority(span, 2);
    int priority = -99;
    int result = ddog_trace_span_get_sampling_priority(span, &priority);
    CHECK(result == 1);
    CHECK(priority == 2);

    ddog_trace_span_finish(span);
    ddog_trace_span_free(span);
}

TEST_CASE("null arguments do not crash", "[c_binding]") {
    // Functions that return handles should return nullptr.
    CHECK(ddog_trace_tracer_new(nullptr) == nullptr);
    CHECK(ddog_trace_tracer_create_span(nullptr, "x") == nullptr);
    CHECK(ddog_trace_span_create_child(nullptr, "x") == nullptr);
    CHECK(ddog_trace_span_create_child_with_options(nullptr, "n", "s", "r") ==
          nullptr);

    char buf[kTraceIdBufSize] = {};
    CHECK(ddog_trace_span_get_trace_id(nullptr, buf, sizeof(buf)) == -1);
    CHECK(ddog_trace_span_get_span_id(nullptr, buf, sizeof(buf)) == -1);

    int priority = 0;
    CHECK(ddog_trace_span_get_sampling_priority(nullptr, &priority) == -1);

    // Void functions with null handles should simply not crash.
    ddog_trace_tracer_conf_free(nullptr);
    ddog_trace_tracer_conf_set(nullptr, DDOG_TRACE_OPT_SERVICE_NAME, "x");
    ddog_trace_tracer_free(nullptr);
    ddog_trace_span_free(nullptr);
    ddog_trace_span_set_tag(nullptr, "k", "v");
    ddog_trace_span_set_error(nullptr, 1);
    ddog_trace_span_set_error_message(nullptr, "msg");
    ddog_trace_span_inject(nullptr, test_header_writer);
    ddog_trace_span_finish(nullptr);
    ddog_trace_span_set_resource(nullptr, "res");
    ddog_trace_span_set_service(nullptr, "svc");
    ddog_trace_span_set_sampling_priority(nullptr, 1);
}
