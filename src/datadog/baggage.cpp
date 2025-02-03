#include <datadog/baggage.h>

#include <cctype>

namespace datadog {
namespace tracing {

namespace {

Expected<std::unordered_map<std::string, std::string>, Baggage::Error>
parse_baggage(StringView input, size_t max_capacity) {
  std::unordered_map<std::string, std::string> result;
  if (input.empty()) return result;

  enum class state : char {
    leading_spaces_key,
    key,
    leading_spaces_value,
    value
  } internal_state = state::leading_spaces_key;

  size_t beg = 0;
  size_t tmp_end = 0;

  StringView key;
  StringView value;

  const size_t end = input.size();

  for (size_t i = 0; i < end; ++i) {
    switch (internal_state) {
      case state::leading_spaces_key: {
        if (!std::isspace(input[i])) {
          if (result.size() == max_capacity)
            return Baggage::Error::MAXIMUM_CAPACITY_REACHED;

          beg = i;
          tmp_end = i;
          internal_state = state::key;
        }
      } break;

      case state::key: {
        if (input[i] == ',') {
          return Baggage::Error::MALFORMED_BAGGAGE_HEADER;
        } else if (input[i] == '=') {
          key = StringView{input.data() + beg, tmp_end - beg + 1};
          internal_state = state::leading_spaces_value;
        } else if (!std::isspace(input[i])) {
          tmp_end = i;
        }
      } break;

      case state::leading_spaces_value: {
        if (!std::isspace(input[i])) {
          beg = i;
          tmp_end = i;
          internal_state = state::value;
        }
      } break;

      case state::value: {
        if (input[i] == ',') {
          value = StringView{input.data() + beg, tmp_end - beg + 1};
          result.emplace(std::string(key), std::string(value));
          beg = i;
          tmp_end = i;
          internal_state = state::leading_spaces_key;
        } else if (!std::isspace(input[i])) {
          tmp_end = i;
        }
      } break;
    }
  }

  if (internal_state != state::value) {
    return Baggage::Error::MALFORMED_BAGGAGE_HEADER;
  }

  value = StringView{input.data() + beg, tmp_end - beg + 1};
  result.emplace(std::string(key), std::string(value));

  return result;
}

}  // namespace

Baggage::Baggage(size_t max_capacity) : max_capacity_(max_capacity) {}

Baggage::Baggage(
    std::initializer_list<std::pair<const std::string, std::string>> baggage,
    size_t max_capacity)
    : max_capacity_(max_capacity), baggage_(baggage) {}

Baggage::Baggage(std::unordered_map<std::string, std::string> baggage,
                 size_t max_capacity)
    : max_capacity_(max_capacity), baggage_(std::move(baggage)) {}

Optional<StringView> Baggage::get(StringView key) const {
  auto it = baggage_.find(std::string(key));
  if (it == baggage_.cend()) return nullopt;

  return it->second;
}

bool Baggage::set(std::string key, std::string value) {
  if (baggage_.size() == max_capacity_) return false;

  baggage_[key] = value;
  return true;
}

void Baggage::remove(StringView key) { baggage_.erase(std::string(key)); }

void Baggage::clear() { baggage_.clear(); }

size_t Baggage::size() const { return baggage_.size(); }

bool Baggage::empty() const { return baggage_.empty(); }

bool Baggage::contains(StringView key) const {
  auto found = baggage_.find(std::string(key));
  return found != baggage_.cend();
}

void Baggage::visit(std::function<void(StringView, StringView)>&& visitor) {
  for (const auto& [key, value] : baggage_) {
    visitor(key, value);
  }
}

Expected<void> Baggage::inject(DictWriter& writer, size_t max_bytes) const {
  std::string res;

  auto it = baggage_.cbegin();
  res += it->first;
  res += "=";
  res += it->second;

  for (it++; it != baggage_.cend(); ++it) {
    res += ",";
    res += it->first;
    res += "=";
    res += it->second;
  }

  if (res.size() >= max_bytes)
    return datadog::tracing::Error{
        datadog::tracing::Error::Code::BAGGAGE_MAXIMUM_BYTES_REACHED, ""};

  /// NOTE(@dmehala): It is the writer's responsibility to write the header,
  /// including percent-encoding.
  writer.set("baggage", res);
  return {};
}

Expected<Baggage, Baggage::Error> Baggage::extract(const DictReader& headers,
                                                   size_t max_capacity) {
  auto found = headers.lookup("baggage");
  if (!found) {
    return Error::MISSING_HEADER;
  }

  auto bv = parse_baggage(*found, max_capacity);
  if (auto error = bv.if_error()) {
    return *error;
  }

  Baggage result(*bv, max_capacity);
  return result;
}

}  // namespace tracing
}  // namespace datadog
