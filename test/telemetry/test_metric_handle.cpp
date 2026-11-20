#include <datadog/telemetry/metric_handle.h>

#include <map>

#include "datadog/telemetry/log_binned_distr.h"
#include "test.h"

using namespace datadog::telemetry;
using datadog::tracing::Optional;

#define METRIC_HANDLE_TEST(x) TEST_CASE(x, "[telemetry][metric_handle]")

namespace {

enum class Step { request_first, request_body };

std::array<const char*, 2> tag_value_names(Step) {
  return {"req.first", "req.body"};
}

struct RequestTags {
  TagString rules_version;
  bool blocked = false;
  Optional<Step> step;

  static auto tag_fields() {
    using Self = RequestTags;
    return std::tuple{tag("rules_version", &Self::rules_version),
                      tag("blocked", &Self::blocked), tag("step", &Self::step)};
  }
};

// Only the first two values have names.
enum class Size { small, large, unnamed };

std::array<const char*, 2> tag_value_names(Size) { return {"small", "large"}; }

struct SizeTags {
  Size size = Size::small;

  static auto tag_fields() { return std::tuple{tag("size", &SizeTags::size)}; }
};

struct TooManyTags {
  TagString first, second, third, fourth, fifth;

  static auto tag_fields() {
    using Self = TooManyTags;
    return std::tuple{tag("first", &Self::first), tag("second", &Self::second),
                      tag("third", &Self::third), tag("fourth", &Self::fourth),
                      tag("fifth", &Self::fifth)};
  }
};

// A handle for TooManyTags would not compile: 65536^5 combinations do not fit
// in a 64-bit series id.
static_assert(!details::fits_in_series_id(
    static_cast<decltype(TooManyTags::tag_fields())*>(nullptr)));
static_assert(details::fits_in_series_id(
    static_cast<decltype(RequestTags::tag_fields())*>(nullptr)));

template <typename Metric>
std::map<std::vector<std::string>, typename Metric::Value> by_tags(
    const std::vector<details::MetricSeries<Metric>>& series) {
  std::map<std::vector<std::string>, typename Metric::Value> result;
  for (const auto& one_series : series) {
    result[one_series.tags] = one_series.value;
  }
  return result;
}

std::string sketch_of(std::initializer_list<double> values) {
  LogBinnedDistr sketch;
  for (double value : values) sketch.record(value);
  return sketch.encode_protobuf();
}

}  // namespace

METRIC_HANDLE_TEST("a counter without tags has one series") {
  details::MetricRegistry registry;
  const auto requests = registry.counter({"requests", "tracers", true},
                                         {"integration_name:datadog"});
  requests->record({}, 1);
  requests->record({}, 1);

  const auto series = registry.take<Counter>();
  REQUIRE(series.size() == 1);
  CHECK(series[0].metric.name == "requests");
  CHECK(series[0].tags == std::vector<std::string>{"integration_name:datadog"});
  CHECK(series[0].value == 2);
  CHECK(registry.take<Counter>().empty());
}

METRIC_HANDLE_TEST("each combination of tag values is a series") {
  details::MetricRegistry registry;
  const auto requests = registry.counter<RequestTags>(
      {"waf.requests", "appsec", true}, {"waf_version:1.30.1"});
  RequestTags tags;
  tags.rules_version = TagString{"1.18.2"};
  requests->record(tags, 1);
  tags.blocked = true;
  tags.step = Step::request_body;
  requests->record(tags, 1);
  tags.rules_version = TagString{"1.18.2"};  // the same string, interned again
  requests->record(tags, 1);

  using Counts = std::map<std::vector<std::string>, std::uint64_t>;
  CHECK(
      by_tags(registry.take<Counter>()) ==
      Counts{
          {{"waf_version:1.30.1", "rules_version:1.18.2", "blocked:false"}, 1},
          {{"waf_version:1.30.1", "rules_version:1.18.2", "blocked:true",
            "step:req.body"},
           2},
      });
}

METRIC_HANDLE_TEST("a dropped handle is forgotten") {
  details::MetricRegistry registry;
  auto requests =
      registry.counter({"requests", "tracers", true}, {"version:1"});
  requests->record({}, 1);
  requests = registry.counter({"requests", "tracers", true}, {"version:2"});
  requests->record({}, 1);

  using Counts = std::map<std::vector<std::string>, std::uint64_t>;
  CHECK(by_tags(registry.take<Counter>()) == Counts{{{"version:2"}, 1}});
}

METRIC_HANDLE_TEST("a rate keeps its last value") {
  details::MetricRegistry registry;
  const auto rate = registry.rate({"spans", "tracers", true});
  rate->record({}, 5);
  rate->record({}, 3);
  CHECK(registry.take<Rate>().at(0).value == 3);
}

METRIC_HANDLE_TEST("a distribution has one sketch per series") {
  details::MetricRegistry registry;
  const auto duration =
      registry.distribution<RequestTags>({"waf.duration", "appsec", true});
  RequestTags tags;
  duration->record(tags, 100);
  duration->record(tags, 250);
  tags.blocked = true;
  duration->record(tags, 7);

  using Sketches = std::map<std::vector<std::string>, EncodedSketch>;
  CHECK(by_tags(registry.take<Distribution>()) ==
        Sketches{
            {{"rules_version:", "blocked:false"}, sketch_of({100, 250})},
            {{"rules_version:", "blocked:true"}, sketch_of({7})},
        });
}

METRIC_HANDLE_TEST("an enum value without a name is dropped") {
  details::MetricRegistry registry;
  const auto bodies = registry.counter<SizeTags>({"bodies", "appsec", true});
  bodies->record({Size::large}, 1);
  bodies->record({Size::unnamed}, 1);

  using Counts = std::map<std::vector<std::string>, std::uint64_t>;
  CHECK(by_tags(registry.take<Counter>()) == Counts{{{"size:large"}, 1}});
}
