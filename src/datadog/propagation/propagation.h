#include <datadog/dict_reader.h>
#include <datadog/logger.h>

#include "extracted_data.h"
#include "span_data.h"

namespace datadog {
namespace tracing {

Expected<ExtractedData> extract_context(
    const DictReader& reader,
    const std::vector<PropagationStyle>& extraction_styles_, Logger* logger_,
    SpanData* span_data);

}  // namespace tracing
}  // namespace datadog
