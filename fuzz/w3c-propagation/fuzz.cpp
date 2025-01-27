#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/null_collector.h>
#include <datadog/optional.h>
#include <datadog/parse_util.h>
#include <datadog/string_view.h>
#include <datadog/tracer.h>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>

namespace dd = datadog::tracing;

namespace {

dd::Tracer& tracer_singleton() {
  thread_local auto tracer = []() {
    dd::TracerConfig config;
    config.service = "fuzzer";
    config.collector = std::make_shared<dd::NullCollector>();
    config.extraction_styles = {dd::PropagationStyle::W3C};

    const auto finalized_config = dd::finalize_config(config);
    if (!finalized_config) {
      std::abort();
    }

    return dd::Tracer{*finalized_config};
  }();

  return tracer;
}

struct MockDictReader : public dd::DictReader {
  dd::StringView traceparent;
  dd::StringView tracestate;

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override {
    if (key == "traceparent") {
      return traceparent;
    }
    if (key == "tracestate") {
      return tracestate;
    }
    return dd::nullopt;
  }

  void visit(
      const std::function<void(dd::StringView key, dd::StringView value)>&
          visitor) const override {
    visitor("traceparent", traceparent);
    visitor("tracestate", tracestate);
  }
};

struct MockDictWriter : public dd::DictWriter {
  void set(dd::StringView, dd::StringView) override {}
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, size_t size) {
  auto& tracer = tracer_singleton();

  const auto begin_traceparent = reinterpret_cast<const char*>(data);
  const auto end = begin_traceparent + size;
  for (const char* begin_tracestate = begin_traceparent;
       begin_tracestate <= end; ++begin_tracestate) {
    MockDictReader reader;
    reader.traceparent =
        dd::StringView(begin_traceparent, begin_tracestate - begin_traceparent);
    reader.tracestate =
        dd::StringView(begin_tracestate, end - begin_tracestate);

    const auto span = tracer.extract_span(reader);
    if (!span) {
      continue;
    }

    MockDictWriter writer;
    span->inject(writer);
  }

  return 0;
}
