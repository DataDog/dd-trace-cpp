#pragma once

namespace datadog {
namespace tracing {

struct PropagationStyles {
    bool datadog = true;
    bool b3 = false;
    bool w3c = false;
};

}  // namespace tracing
}  // namespace datadog
