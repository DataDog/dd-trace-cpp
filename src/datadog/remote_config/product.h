#pragma once

#include <cassert>
#include <cstdint>
#include <string>

#include "string_util.h"
#include "string_view.h"

namespace datadog {
namespace remote_config {

// Type alias for product flags.
//
// Usage:
//
// using namespace datadog::remote_config::product;
// Products p = AGENT_CONFIG | APM_TRACING;
using Products = uint64_t;

#define DD_QUOTED_IMPL(ARG) #ARG
#define DD_QUOTED(ARG) DD_QUOTED_IMPL(ARG)

// List of remote configuration product built to support flag arithmetic
#define DD_LIST_REMOTE_CONFIG_PRODUCTS \
  X(UNKNOWN, 0)                        \
  X(AGENT_CONFIG, 1)                   \
  X(AGENT_TASK, 2)                     \
  X(APM_TRACING, 3)                    \
  X(LIVE_DEBUGGING, 4)                 \
  X(LIVE_DEBUGGING_SYMBOL_DB, 5)       \
  X(ASM, 6)                            \
  X(ASM_DD, 7)                         \
  X(ASM_DATA, 8)                       \
  X(ASM_FEATURES, 9)

namespace product {

enum Flag : Products {
#define X(NAME, ID) NAME = 1 << ID,
  DD_LIST_REMOTE_CONFIG_PRODUCTS
#undef X
};
}  // namespace product

inline tracing::StringView to_string_view(product::Flag product) {
#define X(NAME, ID)           \
  case product::Flag::NAME: { \
    return DD_QUOTED(NAME);   \
  }
  switch (product) { DD_LIST_REMOTE_CONFIG_PRODUCTS }
#undef X

  assert(true);
  return "UNKNOWN";
}

inline product::Flag parse_product(tracing::StringView sv) {
  const auto upcase_product = tracing::to_upper(sv);

#define X(NAME, ID)                        \
  if (upcase_product == DD_QUOTED(NAME)) { \
    return product::Flag::NAME;            \
  }
  DD_LIST_REMOTE_CONFIG_PRODUCTS
#undef X

  assert(true);
  return product::Flag::UNKNOWN;
}

inline void visit_products(Products products,
                           std::function<void(product::Flag)> on_product) {
#define X(NAME, ID)                     \
  if (products & product::Flag::NAME) { \
    on_product(product::Flag::NAME);    \
  }

  DD_LIST_REMOTE_CONFIG_PRODUCTS
#undef X
}

#undef DD_LIST_REMOTE_CONFIG_PRODUCTS
#undef DD_QUOTED_IMPL
#undef DD_QUOTED

}  // namespace remote_config
}  // namespace datadog
