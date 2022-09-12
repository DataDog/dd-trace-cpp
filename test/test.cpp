#include "test.h"

namespace std {

std::ostream& operator<<(std::ostream& stream, const std::pair<const std::string, std::string>& item) {
    return stream << '{' << item.first << ", " << item.second << '}';
}

}  // namespace std
