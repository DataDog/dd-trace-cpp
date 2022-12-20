#include "test.h"

namespace std {

std::ostream& operator<<(
    std::ostream& stream,
    const std::pair<const std::string, std::string>& item) {
  return stream << '{' << item.first << ", " << item.second << '}';
}

std::ostream& operator<<(
    std::ostream& stream,
    const datadog::tracing::Optional<datadog::tracing::StringView>& item) {
  return stream << item.value_or("<nullopt>");
}

}  // namespace std
