// These are tests for `Tracer`.  `Tracer` is responsible for creating root
// spans and for extracting spans from propagated trace context.

#include "catch.hpp"

#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

using namespace datadog::tracing;

// Things to verify:
//
// - span defaults are set on root spans
// - span defaults are set on extracted spans
// - span config overrides defaults on root spans
// - span config overrides defaults on extracted spans
// - extract_or_create yields a root span when there's no context to extract
// - extract returns an error in the following situations:
//     - no parent ID and no trace ID (no context to extract)
//     - parent ID without trace ID
//     - trace ID without parent ID and without origin
//         - but _no error_ when trace ID without parent ID and with origin
//     - both Datadog and B3 extractions styles are set, but they disagree:
//         - difference trace ID
//         - different parent ID
//         - different sampling priority
// - extracted information is as expected when using Datadog style
// - extracted information is as expected when using B3 style
// - extracted information is as expected when using consistent Datadog and B3 style
//     - which info? trace ID, parent span ID, sampling priority
