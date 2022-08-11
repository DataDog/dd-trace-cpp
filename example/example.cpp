#include <datadog/tracer_config.h>

int main() {
    namespace dd = datadog::tracing;
    dd::TracerConfig config;
    (void) config;
}
