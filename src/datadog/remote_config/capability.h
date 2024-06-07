#pragma once

#include <cstdint>
#include <string>

#include "string_util.h"
#include "string_view.h"

namespace datadog {
namespace remote_config {

// Type alias for capabilities flags.
//
// Usage:
//
// using namespace datadog::remote_config::capability;
// Capabilities c = APM_TRACING_SAMPLE_RATE | APM_TRACING_ENABLED;
using Capabilities = uint64_t;

// Type alias for product flags.
//
// Usage:
//
// using namespace datadog::remote_config::product;
// Products p = AGENT_CONFIG | APM_TRACING;
using Products = uint64_t;

namespace capability {

enum Flag : Capabilities {
  // DEFAULT is a special value. It is the default of the product capability.
  DEFAULT = 0,
  ASM_ACTIVATION = 1 << 1,
  ASM_IP_BLOCKING = 1 << 2,
  ASM_DD_RULES = 1 << 3,
  ASM_EXCLUSIONS = 1 << 4,
  ASM_REQUEST_BLOCKING = 1 << 5,
  ASM_RESPONSE_BLOCKING = 1 << 6,
  ASM_USER_BLOCKING = 1 << 7,
  ASM_CUSTOM_RULES = 1 << 8,
  ASM_CUSTOM_BLOCKING_RESPONSE = 1 << 9,
  ASM_TRUSTED_IPS = 1 << 10,
  ASM_API_SECURITY_SAMPLE_RATE = 1 << 11,
  APM_TRACING_SAMPLE_RATE = 1 << 12,
  APM_TRACING_LOGS_INJECTION = 1 << 13,
  APM_TRACING_HTTP_HEADER_TAGS = 1 << 14,
  APM_TRACING_TAGS = 1 << 15,
  ASM_PREPROCESSOR_OVERRIDES = 1 << 16,
  ASM_CUSTOM_DATA_SCANNERS = 1 << 17,
  ASM_EXCLUSION_DATA = 1 << 18,
  APM_TRACING_ENABLED = 1 << 19,
  APM_TRACING_DATA_STREAMS_ENABLED = 1 << 20,
  ASM_RASP_SQLI = 1 << 21,
  ASM_RASP_LFI = 1 << 22,
  ASM_RASP_SSRF = 1 << 23,
  ASM_RASP_SHI = 1 << 24,
  ASM_RASP_XXE = 1 << 25,
  ASM_RASP_RCE = 1 << 26,
  ASM_RASP_NOSQLI = 1 << 27,
  ASM_RASP_XSS = 1 << 28,
  APM_TRACING_SAMPLE_RULES = 1 << 29,
};

}  // namespace capability

#define DD_QUOTED_IMPL(ARG) #ARG
#define DD_QUOTED(ARG) DD_QUOTED_IMPL(ARG)

// List of remote configuration product built to support flag arithmetic
#define DD_LIST_REMOTE_CONFIG_PRODUCTS \
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

  std::abort();
}

inline product::Flag parse_product(tracing::StringView sv) {
  const auto upcase_product = tracing::to_upper(sv);

#define X(NAME, ID)                        \
  if (upcase_product == DD_QUOTED(NAME)) { \
    return product::Flag::NAME;            \
  }
  DD_LIST_REMOTE_CONFIG_PRODUCTS
#undef X

  std::abort();
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
