#include <filesystem>

namespace datadog::tracing {
class Tracer;
}  // namespace datadog::tracing

// Use the specified `tracer` to create a trace whose structure resembles the
// file system tree rooted at the specified `path`.
void sha256_traced(const std::filesystem::path &path,
                   datadog::tracing::Tracer &tracer);
