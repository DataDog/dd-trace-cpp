#include <datadog/baggage.h>

#include <sstream>

namespace datadog {
namespace tracing {

namespace {

std::string trim(const std::string& str) {
  size_t start = str.find_first_not_of(' ');
  size_t end = str.find_last_not_of(' ');
  return (start == std::string::npos || end == std::string::npos)
             ? ""
             : str.substr(start, end - start + 1);
}

Expected<std::unordered_map<std::string, std::string>, Baggage::Error>
parse_baggage(StringView input, size_t max_capacity) {
  std::unordered_map<std::string, std::string> result;
  std::stringstream ss(std::string{input});
  std::string pair;

  // Split by commas
  while (std::getline(ss, pair, ',')) {
    size_t equalPos = pair.find('=');

    if (equalPos == std::string::npos)
      return Baggage::Error::MALFORMED_BAGGAGE_HEADER;

    // Extract key and value, then trim spaces
    std::string key = trim(pair.substr(0, equalPos));
    if (key.empty()) return Baggage::Error::MALFORMED_BAGGAGE_HEADER;
    if (result.size() == max_capacity)
      return Baggage::Error::MAXIMUM_CAPACITY_REACHED;

    std::string value = trim(pair.substr(equalPos + 1));
    result[key] = value;
  }
  return result;
}

/*constexpr bool is_space(char c) {*/
/*  return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c ==
 * '\v';*/
/*}*/

/*Expected<std::unordered_map<std::string, std::string>, Baggage::Error>*/
/*parse_baggage(StringView input, size_t max_capacity) {*/
/*  enum class state : char {*/
/*    swallow,*/
/*    key,*/
/*    value*/
/*  } internal_state = state::swallow;*/
/**/
/*  std::unordered_map<std::string, std::string> result;*/
/**/
/*  size_t beg = 0;*/
/*  size_t tmp_end = 0;*/
/**/
/*  StringView key;*/
/*  StringView value;*/
/**/
/*  const size_t end = input.size();*/
/**/
/*  for (size_t i = 0; i < end; ++i) {*/
/*    switch(internal_state) {*/
/*      case state::swallow: {*/
/*        if (!is_space(input[i])) {*/
/*          beg = i;*/
/*          internal_state = state::key;*/
/*        }*/
/*      } break;*/
/**/
/*      case state::key: {*/
/*        if (input[i] == '=') {*/
/*          if (result.size() == max_capacity) return
 * Baggage::Error::MAXIMUM_CAPACITY_REACHED;*/
/**/
/*          key = StringView{input.data() + beg, tmp_end - beg};*/
/*          beg = i;*/
/*          tmp_end = i;*/
/*          internal_state = state::value;*/
/*        } else if (!is_space(input[i])) {*/
/*          tmp_end = i;*/
/*        }*/
/*      } break;*/
/**/
/*      case state::value: {*/
/*        if (input[i] == ',') {*/
/*          value = StringView{input.data() + beg, tmp_end - beg};*/
/*          result.emplace(std::string(key), std::string(value));*/
/*          beg = i;*/
/*          tmp_end = i;*/
/*          internal_state = state::swallow;*/
/*        } else if (!is_space(input[i])) {*/
/*          tmp_end = i;*/
/*        }*/
/*      } break;*/
/**/
/*    }*/
/*  }*/
/**/
/*  if (internal_state != state::value) {*/
/*    return Baggage::Error::MALFORMED_BAGGAGE_HEADER;*/
/*  }*/
/**/
/*  value = StringView{input.data() + beg, tmp_end - beg};*/
/*  result.emplace(std::string(key), std::string(value));*/
/**/
/*  return result;*/
/*}*/

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
