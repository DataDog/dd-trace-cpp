#pragma once

#include <algorithm>
#include <sstream>

#include "test.h"

template <typename Map>
class ContainsSubset : public Catch::MatcherBase<Map> {
  const Map* subset_;

 public:
  ContainsSubset(const Map& subset) : subset_(&subset) {}

  bool match(const Map& other) const override {
    return std::all_of(subset_->begin(), subset_->end(), [&](const auto& item) {
      const auto& [key, value] = item;
      auto found = other.find(key);
      return found != other.end() && found->second == value;
    });
  }

  std::string describe() const override {
    std::ostringstream stream;
    stream << "ContainsSubset: {";
    for (const auto& [key, value] : *subset_) {
      stream << "  {";
      stream << key;
      stream << ", ";
      stream << value;
      stream << "}";
    }
    stream << "  }";
    return stream.str();
  }
};
