#include <datadog/telemetry/metric_handle.h>

#include "log_binned_distr.h"

namespace datadog::telemetry {
namespace {

// Id of every new string once the table is full.
constexpr std::uint16_t overflow_id = 0xFFFF;

// The strings of TagString values. Id 0 is the empty string.
class StringTable {
 public:
  static StringTable& instance() {
    static StringTable table;
    return table;
  }

  std::uint16_t intern(tracing::StringView value);
  std::string value(std::uint16_t id);

 private:
  StringTable() { add(""); }
  std::uint16_t add(const std::string& value);

  std::mutex mutex_;
  std::vector<std::string> strings_;
  std::unordered_map<std::string, std::uint16_t> ids_;
};

std::vector<details::SeriesId> series_id_strides(
    const std::vector<std::uint64_t>& value_counts);

}  // namespace

TagString::TagString(tracing::StringView value)
    : id_(StringTable::instance().intern(value)) {}

std::string TagString::value() const { return details::interned_string(id_); }

namespace details {

std::string interned_string(std::uint64_t id) {
  return StringTable::instance().value(static_cast<std::uint16_t>(id));
}

std::vector<std::string> render_names(
    const std::string& name, const std::vector<std::string>& value_names) {
  std::vector<std::string> rendered;
  for (const std::string& value_name : value_names) {
    rendered.push_back(name + ":" + value_name);
  }
  return rendered;
}

template <typename Metric>
MetricBase<Metric>::MetricBase(
    Metric metric, std::vector<std::string> fixed_tags,
    std::vector<std::uint64_t> value_counts,
    std::vector<std::unique_ptr<const TagCodec>> tags)
    : metric_(std::move(metric)),
      fixed_tags_(std::move(fixed_tags)),
      value_counts_(std::move(value_counts)),
      tags_(std::move(tags)),
      strides_(series_id_strides(value_counts_)) {}

template <typename Metric>
tracing::Optional<SeriesId> MetricBase<Metric>::series_id(
    const void* tags) const {
  SeriesId id = 0;
  for (std::size_t index = 0; index < tags_.size(); ++index) {
    const std::uint64_t value = tags_[index]->read(tags);
    if (value >= value_counts_[index]) return tracing::nullopt;
    id += value * strides_[index];
  }
  return id;
}

template <typename Metric>
std::vector<std::string> MetricBase<Metric>::series_tags(SeriesId id) const {
  std::vector<std::string> tags = fixed_tags_;
  for (std::size_t index = 0; index < tags_.size(); ++index) {
    const std::uint64_t value = id / strides_[index] % value_counts_[index];
    std::string rendered = tags_[index]->render(value);
    if (!rendered.empty()) tags.push_back(std::move(rendered));
  }
  return tags;
}

template class MetricBase<Counter>;
template class MetricBase<Rate>;
template class MetricBase<Distribution>;

struct SeriesSketches::Impl {
  std::mutex mutex;
  std::unordered_map<SeriesId, LogBinnedDistr> sketches;
};

SeriesSketches::SeriesSketches() : impl_(std::make_unique<Impl>()) {}

SeriesSketches::~SeriesSketches() = default;

void SeriesSketches::record(SeriesId series, std::uint64_t value) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->sketches[series].record(static_cast<double>(value));
}

std::vector<std::pair<SeriesId, EncodedSketch>> SeriesSketches::take() {
  std::unordered_map<SeriesId, LogBinnedDistr> taken;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    taken.swap(impl_->sketches);
  }
  std::vector<std::pair<SeriesId, EncodedSketch>> encoded;
  for (const auto& [series, sketch] : taken) {
    encoded.emplace_back(series, sketch.encode_protobuf());
  }
  return encoded;
}

}  // namespace details

namespace {

std::uint16_t StringTable::intern(tracing::StringView value) {
  const std::string key(value);
  std::lock_guard<std::mutex> lock(mutex_);
  if (const auto found = ids_.find(key); found != ids_.end()) {
    return found->second;
  }
  if (strings_.size() >= overflow_id) return overflow_id;
  return add(key);
}

std::string StringTable::value(std::uint16_t id) {
  if (id == overflow_id) return "other";
  std::lock_guard<std::mutex> lock(mutex_);
  return id < strings_.size() ? strings_[id] : std::string();
}

// Expects the lock to be held, or the table to be under construction.
std::uint16_t StringTable::add(const std::string& value) {
  const auto id = static_cast<std::uint16_t>(strings_.size());
  strings_.push_back(value);
  ids_.emplace(value, id);
  return id;
}

// The last tag has stride 1. Each other tag's stride is the number of
// combinations of the values of the tags after it.
std::vector<details::SeriesId> series_id_strides(
    const std::vector<std::uint64_t>& value_counts) {
  std::vector<details::SeriesId> strides(value_counts.size());
  details::SeriesId stride = 1;
  for (std::size_t index = value_counts.size(); index-- > 0;) {
    strides[index] = stride;
    stride *= value_counts[index];
  }
  return strides;
}

}  // namespace
}  // namespace datadog::telemetry
