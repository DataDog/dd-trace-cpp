#pragma once

// Metrics registered once with all their tags. Recording a point then takes
// plain values: no tag strings are built, copied or hashed. A
// details::MetricRegistry creates the handles and takes their series.
//
// A struct describes the tags. Its static `tag_fields()` returns a std::tuple
// of tag() calls, which bind each field to a tag name. String tags come in two
// kinds:
// * values known in advance: an enum field. `tag_value_names(Enum{})`, found
//   by argument-dependent lookup, returns a std::array with one name per
//   value, in order.
// * values not known in advance: a TagString field.
// test/telemetry/test_metric_handle.cpp has examples.
//
// Telemetry does not collect these handles yet.

#include <datadog/optional.h>
#include <datadog/string_view.h>
#include <datadog/telemetry/metrics.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace datadog::telemetry {

// A tag value not known in advance, such as a version. Creating one stores
// the string in a global table; copying it copies a small number. The table
// holds at most 65535 strings. Later new strings all read as "other".
class TagString {
 public:
  static constexpr std::uint64_t id_count = std::uint64_t{1} << 16;

  // The empty string.
  TagString() = default;
  explicit TagString(tracing::StringView value);

  std::uint16_t id() const { return id_; }
  std::string value() const;

 private:
  std::uint16_t id_ = 0;
};

namespace details {

// Identifies one series of a metric: one combination of its tag values. Each
// tag's value is multiplied by the tag's stride, and the results added.
using SeriesId = std::uint64_t;

// One tag of a metric: reads the tag's field from a tag struct, and renders
// the tag's values. The tag struct's type is hidden, so that MetricBase does
// not depend on it.
class TagCodec {
 public:
  virtual ~TagCodec() = default;
  // The value of the tag's field in `*tags`, as a number. `tags` points to the
  // tag struct that the codec was made for.
  virtual std::uint64_t read(const void* tags) const = 0;
  // "name:value", or an empty string if the tag is not sent.
  virtual std::string render(std::uint64_t value) const = 0;
};

std::string interned_string(std::uint64_t id);
// "name:value_name" for each value name.
std::vector<std::string> render_names(
    const std::string& name, const std::vector<std::string>& value_names);

// A bool or enum field. Value i renders as rendered[i].
template <typename Tags, typename Field>
class NamedTag final : public TagCodec {
 public:
  NamedTag(Field Tags::*field, std::vector<std::string> rendered)
      : field_(field), rendered_(std::move(rendered)) {}

  std::uint64_t read(const void* tags) const override {
    return static_cast<std::uint64_t>(static_cast<const Tags*>(tags)->*field_);
  }

  std::string render(std::uint64_t value) const override {
    return rendered_[value];
  }

 private:
  Field Tags::*field_;
  std::vector<std::string> rendered_;
};

// An optional enum field. Value 0 is an empty field, which is not sent. Value
// i + 1 renders as rendered[i].
template <typename Tags, typename Enum>
class OptionalTag final : public TagCodec {
 public:
  OptionalTag(tracing::Optional<Enum> Tags::*field,
              std::vector<std::string> rendered)
      : field_(field), rendered_(std::move(rendered)) {}

  std::uint64_t read(const void* tags) const override {
    const auto& value = static_cast<const Tags*>(tags)->*field_;
    return value ? static_cast<std::uint64_t>(*value) + 1 : 0;
  }

  std::string render(std::uint64_t value) const override {
    return value == 0 ? std::string() : rendered_[value - 1];
  }

 private:
  tracing::Optional<Enum> Tags::*field_;
  std::vector<std::string> rendered_;
};

// A TagString field. The value is the string's id.
template <typename Tags>
class StringTag final : public TagCodec {
 public:
  StringTag(TagString Tags::*field, std::string name)
      : field_(field), name_(std::move(name)) {}

  std::uint64_t read(const void* tags) const override {
    return (static_cast<const Tags*>(tags)->*field_).id();
  }

  std::string render(std::uint64_t value) const override {
    return name_ + ":" + interned_string(value);
  }

 private:
  TagString Tags::*field_;
  std::string name_;
};

template <typename Enum>
constexpr std::uint64_t enum_value_count() {
  return std::tuple_size<
      std::decay_t<decltype(tag_value_names(Enum{}))>>::value;
}

template <typename Enum>
std::vector<std::string> enum_value_names() {
  std::vector<std::string> names;
  for (const char* name : tag_value_names(Enum{})) names.emplace_back(name);
  return names;
}

// One tag of a metric. Build it with tag(). The tag's values are numbered
// from 0 to ValueCount - 1.
template <typename Tags, std::uint64_t ValueCount>
struct TagField {
  using TagStruct = Tags;
  static constexpr std::uint64_t value_count = ValueCount;
  std::unique_ptr<const TagCodec> codec;
};

// True if each combination of the fields' values has its own SeriesId.
template <typename... Fields>
constexpr bool fits_in_series_id(const std::tuple<Fields...>*) {
  std::uint64_t combinations = 1;
  for (std::uint64_t count :
       std::initializer_list<std::uint64_t>{Fields::value_count...}) {
    if (count == 0 ||
        combinations > std::numeric_limits<SeriesId>::max() / count) {
      return false;
    }
    combinations *= count;
  }
  return true;
}

// Tags of a metric that has only fixed tags.
struct NoTags {
  static std::tuple<> tag_fields() { return {}; }
};

// One series of a metric: the metric, the series' tags and its value.
template <typename Metric>
struct MetricSeries {
  Metric metric;
  std::vector<std::string> tags;
  typename Metric::Value value;
};

class MetricRegistry;

// The integer of each series. Counters add the values recorded for a series;
// rates keep the last one. Thread-safe.
template <bool keep_last>
class SeriesIntegers {
 public:
  using Value = std::uint64_t;

  void record(SeriesId series, std::uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    Value& current = values_[series];
    current = keep_last ? value : current + value;
  }

  std::vector<std::pair<SeriesId, Value>> take() {
    std::unordered_map<SeriesId, Value> taken;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      taken.swap(values_);
    }
    return {taken.begin(), taken.end()};
  }

 private:
  std::mutex mutex_;
  std::unordered_map<SeriesId, Value> values_;
};

// The sketch of each series. Recording adds the value to it. Thread-safe.
class SeriesSketches {
 public:
  using Value = EncodedSketch;

  SeriesSketches();
  ~SeriesSketches();

  void record(SeriesId series, std::uint64_t value);
  std::vector<std::pair<SeriesId, Value>> take();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Everything about a metric handle that depends neither on its Tags type nor
// on how it stores its values.
template <typename Metric>
class MetricBase {
 public:
  // Removes the series recorded so far, and appends them to `series`.
  virtual void take_series(std::vector<MetricSeries<Metric>>& series) = 0;

 protected:
  // Handles are not deleted through MetricBase.
  ~MetricBase() = default;

  MetricBase(Metric metric, std::vector<std::string> fixed_tags,
             std::vector<std::uint64_t> value_counts,
             std::vector<std::unique_ptr<const TagCodec>> tags);

  const Metric& metric() const { return metric_; }
  // The series of the tag struct `*tags`. Empty if a value is out of range.
  tracing::Optional<SeriesId> series_id(const void* tags) const;
  // The fixed tags, then the tags of series `id` that are sent.
  std::vector<std::string> series_tags(SeriesId id) const;

 private:
  Metric metric_;
  std::vector<std::string> fixed_tags_;
  std::vector<std::uint64_t> value_counts_;
  std::vector<std::unique_ptr<const TagCodec>> tags_;
  std::vector<SeriesId> strides_;
};

// A metric registered with all its tags. Only a MetricRegistry creates them.
// `Store` keeps the value of each series: record() combines a new value into
// it, and take() removes them. record() is thread-safe.
template <typename Metric, typename Tags, typename Store>
class MetricHandle final : public MetricBase<Metric> {
  static_assert(
      fits_in_series_id(static_cast<decltype(Tags::tag_fields())*>(nullptr)),
      "Too many combinations of tag values for a 64-bit SeriesId");
  static_assert(
      std::is_same_v<typename decltype(std::declval<Store&>()
                                           .take())::value_type::second_type,
                     typename Metric::Value>,
      "The store's values must have the metric's Value type");

 public:
  void record(const Tags& tags, std::uint64_t value) {
    if (const auto series = this->series_id(&tags)) {
      store_.record(*series, value);
    }
  }

  void take_series(std::vector<MetricSeries<Metric>>& series) override {
    for (auto& [id, value] : store_.take()) {
      series.push_back(
          {this->metric(), this->series_tags(id), std::move(value)});
    }
  }

 private:
  friend class MetricRegistry;

  MetricHandle(Metric metric, std::vector<std::string> fixed_tags)
      : MetricHandle(std::move(metric), std::move(fixed_tags),
                     Tags::tag_fields()) {}

  // The codecs read `Tags` structs, so every field must be one of Tags.
  template <typename... Fields>
  MetricHandle(Metric metric, std::vector<std::string> fixed_tags,
               std::tuple<Fields...> fields)
      : MetricBase<Metric>(std::move(metric), std::move(fixed_tags),
                           {Fields::value_count...},
                           codecs(std::move(fields))) {
    static_assert((std::is_same_v<typename Fields::TagStruct, Tags> && ...),
                  "tag_fields() must only bind fields of its own struct");
  }

  template <typename... Fields>
  static std::vector<std::unique_ptr<const TagCodec>> codecs(
      std::tuple<Fields...> fields) {
    std::vector<std::unique_ptr<const TagCodec>> codecs;
    std::apply(
        [&codecs](auto&... field) {
          (codecs.push_back(std::move(field.codec)), ...);
        },
        fields);
    return codecs;
  }

  Store store_;
};

}  // namespace details

template <typename Tags>
details::TagField<Tags, 2> tag(const std::string& name, bool Tags::*field) {
  return {std::make_unique<details::NamedTag<Tags, bool>>(
      field, details::render_names(name, {"false", "true"}))};
}

template <typename Tags, typename Enum,
          typename = std::enable_if_t<std::is_enum_v<Enum>>>
details::TagField<Tags, details::enum_value_count<Enum>()> tag(
    const std::string& name, Enum Tags::*field) {
  return {std::make_unique<details::NamedTag<Tags, Enum>>(
      field, details::render_names(name, details::enum_value_names<Enum>()))};
}

// An empty field is not sent.
template <typename Tags, typename Enum>
details::TagField<Tags, details::enum_value_count<Enum>() + 1> tag(
    const std::string& name, tracing::Optional<Enum> Tags::*field) {
  return {std::make_unique<details::OptionalTag<Tags, Enum>>(
      field, details::render_names(name, details::enum_value_names<Enum>()))};
}

template <typename Tags>
details::TagField<Tags, TagString::id_count> tag(const std::string& name,
                                                 TagString Tags::*field) {
  return {std::make_unique<details::StringTag<Tags>>(field, name)};
}

// Recording a value adds it to the series' count.
template <typename Tags = details::NoTags>
using CounterHandle =
    details::MetricHandle<Counter, Tags,
                          details::SeriesIntegers</*keep_last=*/false>>;

// Recording a value replaces the series' rate.
template <typename Tags = details::NoTags>
using RateHandle =
    details::MetricHandle<Rate, Tags,
                          details::SeriesIntegers</*keep_last=*/true>>;

// Recording a value adds it to the series' sketch.
template <typename Tags = details::NoTags>
using DistributionHandle =
    details::MetricHandle<Distribution, Tags, details::SeriesSketches>;

namespace details {

// Creates metric handles, and takes the series of those still in use. The
// registry holds only weak references: a handle that its caller drops is
// forgotten, with the values it recorded since the last take. Creating and
// taking lock the registry; recording does not.
class MetricRegistry {
 public:
  template <typename Tags = NoTags>
  std::shared_ptr<CounterHandle<Tags>> counter(
      Counter metric, std::vector<std::string> fixed_tags = {}) {
    return add<CounterHandle<Tags>>(std::move(metric), std::move(fixed_tags));
  }

  template <typename Tags = NoTags>
  std::shared_ptr<RateHandle<Tags>> rate(
      Rate metric, std::vector<std::string> fixed_tags = {}) {
    return add<RateHandle<Tags>>(std::move(metric), std::move(fixed_tags));
  }

  template <typename Tags = NoTags>
  std::shared_ptr<DistributionHandle<Tags>> distribution(
      Distribution metric, std::vector<std::string> fixed_tags = {}) {
    return add<DistributionHandle<Tags>>(std::move(metric),
                                         std::move(fixed_tags));
  }

  // Removes and returns the series that the handles of `Metric` recorded
  // since the last call. Forgets the handles that were dropped.
  template <typename Metric>
  std::vector<MetricSeries<Metric>> take() {
    std::vector<MetricSeries<Metric>> series;
    std::lock_guard<std::mutex> lock(mutex_);
    auto& handles = std::get<Handles<Metric>>(handles_);
    for (const auto& weak_handle : handles) {
      if (const auto handle = weak_handle.lock()) handle->take_series(series);
    }
    handles.erase(std::remove_if(handles.begin(), handles.end(),
                                 [](const auto& weak_handle) {
                                   return weak_handle.expired();
                                 }),
                  handles.end());
    return series;
  }

 private:
  template <typename Metric>
  using Handles = std::vector<std::weak_ptr<details::MetricBase<Metric>>>;

  template <typename Handle, typename Metric>
  std::shared_ptr<Handle> add(Metric metric,
                              std::vector<std::string> fixed_tags) {
    std::shared_ptr<Handle> handle(
        new Handle(std::move(metric), std::move(fixed_tags)));
    std::lock_guard<std::mutex> lock(mutex_);
    std::get<Handles<Metric>>(handles_).push_back(handle);
    return handle;
  }

  std::mutex mutex_;
  std::tuple<Handles<Counter>, Handles<Rate>, Handles<Distribution>> handles_;
};

}  // namespace details
}  // namespace datadog::telemetry
