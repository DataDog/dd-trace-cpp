#include "span.h"

#include <cassert>
#include <string>

#include "debug_span.h"
#include "dict_writer.h"
#include "optional.h"
#include "span_config.h"
#include "span_data.h"
#include "string_view.h"
#include "tags.h"
#include "trace_segment.h"

namespace datadog {
namespace tracing {

Span::Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment,
           const std::function<std::uint64_t()>& generate_span_id,
           const Clock& clock, Span* debug_parent)
    : trace_segment_(trace_segment),
      data_(data),
      generate_span_id_(generate_span_id),
      clock_(clock) {
  assert(trace_segment_);
  assert(data_);
  assert(generate_span_id_);
  assert(clock_);

  if (debug_parent) {
    SpanConfig config;
    config.start = start_time();
    config.tags["span_id"] = std::to_string(id());
    debug_span_ = std::make_unique<Span>(debug_parent->create_child(config));
  }
}

Span::~Span() {
  if (!trace_segment_) {
    // We were moved from.
    return;
  }

  if (end_time_) {
    data_->duration = *end_time_ - data_->start.tick;
  } else {
    const auto now = clock_();
    data_->duration = now - data_->start;
  }

  // TODO: debug span
  trace_segment_->span_finished();
}

Span Span::create_child(const SpanConfig& config) const {
  DebugSpan debug{debug_span_.get()};

  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(trace_segment_->defaults(), config, clock_);
  span_data->trace_id = data_->trace_id;
  span_data->parent_id = data_->span_id;
  span_data->span_id = generate_span_id_();

  debug.apply([&](Span& span) {
    span.set_name("create_child");
    span.set_tag("metatrace.span.id", std::to_string(span_data->span_id));
    span.set_tag("metatrace.span.service", span_data->service);
    span.set_tag("metatrace.span.name", span_data->name);
    span.set_tag("metatrace.span.resource", span_data->resource);
  });

  const auto span_data_ptr = span_data.get();
  trace_segment_->register_span(std::move(span_data));
  return Span(span_data_ptr, trace_segment_, generate_span_id_, clock_,
              debug_span_.get());
}

Span Span::create_child() const { return create_child(SpanConfig{}); }

void Span::inject(DictWriter& writer) const {
  // TODO: debug span
  trace_segment_->inject(writer, *data_);
}

std::uint64_t Span::id() const { return data_->span_id; }

TraceID Span::trace_id() const { return data_->trace_id; }

Optional<std::uint64_t> Span::parent_id() const {
  if (data_->parent_id == 0) {
    return nullopt;
  }
  return data_->parent_id;
}

TimePoint Span::start_time() const { return data_->start; }

bool Span::error() const { return data_->error; }

const std::string& Span::service_name() const { return data_->service; }

const std::string& Span::service_type() const { return data_->service_type; }

const std::string& Span::name() const { return data_->name; }

const std::string& Span::resource_name() const { return data_->resource; }

Optional<StringView> Span::lookup_tag(StringView name) const {
  if (tags::is_internal(name)) {
    return nullopt;
  }

  const auto found = data_->tags.find(std::string(name));
  if (found == data_->tags.end()) {
    return nullopt;
  }
  return found->second;
}

void Span::set_tag(StringView name, StringView value) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&](Span& span) {
    span.set_name("set_tag");
    span.set_tag("metatrace.tag.name", name);
    span.set_tag("metatrace.tag.proposed_value", value);
  });

  if (tags::is_internal(name)) {
    debug.apply(
        [&](Span& span) { span.set_tag("metatrace.tag_internal", name); });
    return;
  }

  std::string name_str{name};
  const auto found = data_->tags.find(name_str);
  if (found != data_->tags.end()) {
    debug.apply([&](Span& span) {
      span.set_tag("metatrace.tag.previous_value", found->second);
    });
    assign(found->second, value);
  } else {
    data_->tags.emplace(std::move(name_str), std::string(value));
  }
}

void Span::remove_tag(StringView name) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&](Span& span) {
    span.set_name("remove_tag");
    span.set_tag("metatrace.tag.name", name);
  });

  if (tags::is_internal(name)) {
    debug.apply(
        [&](Span& span) { span.set_tag("metatrace.tag.internal", name); });
    return;
  }

  std::string name_str{name};
  const auto found = data_->tags.find(name_str);
  if (found != data_->tags.end()) {
    debug.apply([&](Span& span) {
      span.set_tag("metatrace.tag.previous_value", found->second);
    });
    data_->tags.erase(found);
  }
}

void Span::set_service_name(StringView service) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_service");
    span.set_tag("metatrace.service.previous_value", data_->service);
    span.set_tag("metatrace.service.proposed_value", service);
  });
  assign(data_->service, service);
}

void Span::set_service_type(StringView type) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_service_type");
    span.set_tag("metatrace.service_type.previous_value", data_->service_type);
    span.set_tag("metatrace.service_type.proposed_value", type);
  });
  assign(data_->service_type, type);
}

void Span::set_resource_name(StringView resource) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_resource_name");
    span.set_tag("metatrace.resource_name.previous_value", data_->resource);
    span.set_tag("metatrace.resource_name.proposed_value", resource);
  });
  assign(data_->resource, resource);
}

void Span::set_error(bool is_error) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_error");
    span.set_tag("metatrace.error.previous_value",
                 data_->error ? "true" : "false");
    span.set_tag("metatrace.error.proposed_value", is_error ? "true" : "false");
  });

  data_->error = is_error;
  if (!is_error) {
    data_->tags.erase("error.message");
    data_->tags.erase("error.type");
  }
}

void Span::set_error_message(StringView message) {
  const auto found = data_->tags.find("error.message");

  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_error_message");
    span.set_tag("metatrace.error.previous_value",
                 data_->error ? "true" : "false");
    span.set_tag("metatrace.error.message.proposed_value", message);
    if (found != data_->tags.end()) {
      span.set_tag("metatrace.error.message.previous_value", found->second);
    }
  });

  data_->error = true;
  if (found != data_->tags.end()) {
    assign(found->second, message);
  } else {
    data_->tags.emplace("error.message", std::string(message));
  }
}

void Span::set_error_type(StringView type) {
  const auto found = data_->tags.find("error.type");

  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_error_type");
    span.set_tag("metatrace.error.previous_value",
                 data_->error ? "true" : "false");
    span.set_tag("metatrace.error.type.proposed_value", type);
    if (found != data_->tags.end()) {
      span.set_tag("metatrace.error.type.previous_value", found->second);
    }
  });

  data_->error = true;
  if (found != data_->tags.end()) {
    assign(found->second, type);
  } else {
    data_->tags.emplace("error.type", std::string(type));
  }
}

void Span::set_error_stack(StringView stack) {
  const auto found = data_->tags.find("error.stack");

  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_error_stack");
    span.set_tag("metatrace.error.previous_value",
                 data_->error ? "true" : "false");
    span.set_tag("metatrace.error.stack.proposed_value", stack);
    if (found != data_->tags.end()) {
      span.set_tag("metatrace.error.stack.previous_value", found->second);
    }
  });

  data_->error = true;
  if (found != data_->tags.end()) {
    assign(found->second, stack);
  } else {
    data_->tags.emplace("error.stack", std::string(stack));
  }
}

void Span::set_name(StringView value) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_name");
    span.set_tag("metatrace.name.proposed_value", value);
    span.set_tag("metatrace.name.previous_value", data_->name);
  });

  assign(data_->name, value);
}

void Span::set_end_time(std::chrono::steady_clock::time_point end_time) {
  DebugSpan debug{debug_span_.get()};
  debug.apply([&, this](Span& span) {
    span.set_name("set_end_time");
    span.set_tag(
        "metatrace.end_time.proposed_value",
        std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           end_time.time_since_epoch())
                           .count()));
    span.set_tag(
        "metatrace.duration.proposed_value",
        std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           end_time - start_time().tick)
                           .count()));
    if (end_time_) {
      span.set_tag(
          "metatrace.end_time.previous_value",
          std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             end_time_->time_since_epoch())
                             .count()));
      span.set_tag(
          "metatrace.duration.previous_value",
          std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             *end_time_ - start_time().tick)
                             .count()));
    }
    span.set_end_time(end_time);
  });

  end_time_ = end_time;
}

TraceSegment& Span::trace_segment() { return *trace_segment_; }

const TraceSegment& Span::trace_segment() const { return *trace_segment_; }

}  // namespace tracing
}  // namespace datadog
