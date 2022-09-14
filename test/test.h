#define CATCH_CONFIG_CPP17_UNCAUGHT_EXCEPTIONS
#define CATCH_CONFIG_CPP17_STRING_VIEW
#define CATCH_CONFIG_CPP17_VARIANT
#define CATCH_CONFIG_CPP17_OPTIONAL
#define CATCH_CONFIG_CPP17_BYTE

#include <iosfwd>
#include <string>
#include <utility>

#include "catch.hpp"

namespace std {

std::ostream& operator<<(std::ostream& stream,
                         const std::pair<const std::string, std::string>& item);

}  // namespace std
