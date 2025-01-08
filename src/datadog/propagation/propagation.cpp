#include "propagation.h"

#include <assert.h>

#include "extraction_util.h"
#include "w3c_propagation.h"

namespace datadog {
namespace tracing {

Expected<ExtractedData> extract_context(
    const DictReader& reader,
    const std::vector<PropagationStyle>& extraction_styles_, Logger* logger_,
    SpanData* span_data) {
  AuditedReader audited_reader{reader};

  Optional<PropagationStyle> first_style_with_trace_id;
  Optional<PropagationStyle> first_style_with_parent_id;
  std::unordered_map<PropagationStyle, ExtractedData> extracted_contexts;

  for (const auto style : extraction_styles_) {
    using Extractor = decltype(&extract_datadog);  // function pointer
    Extractor extract;
    switch (style) {
      case PropagationStyle::DATADOG:
        extract = &extract_datadog;
        break;
      case PropagationStyle::B3:
        extract = &extract_b3;
        break;
      case PropagationStyle::W3C:
        extract = &extract_w3c;
        break;
      default:
        assert(style == PropagationStyle::NONE);
        extract = &extract_none;
    }
    audited_reader.entries_found.clear();
    auto data = extract(audited_reader, span_data->tags, *logger_);
    if (auto* error = data.if_error()) {
      return error->with_prefix(
          extraction_error_prefix(style, audited_reader.entries_found));
    }

    if (!first_style_with_trace_id && data->trace_id.has_value()) {
      first_style_with_trace_id = style;
    }

    if (!first_style_with_parent_id && data->parent_id.has_value()) {
      first_style_with_parent_id = style;
    }

    data->headers_examined = audited_reader.entries_found;
    extracted_contexts.emplace(style, std::move(*data));
  }

  ExtractedData merged_context;
  if (!first_style_with_trace_id) {
    // Nothing extracted a trace ID. Return the first context that includes a
    // parent ID, if any, or otherwise just return an empty `ExtractedData`.
    // The purpose of looking for a parent ID is to allow for the error
    // "extracted a parent ID without a trace ID," if that's what happened.
    if (first_style_with_parent_id) {
      auto other = extracted_contexts.find(*first_style_with_parent_id);
      assert(other != extracted_contexts.end());
      merged_context = other->second;
    }
  } else {
    merged_context = merge(*first_style_with_trace_id, extracted_contexts);
  }

  return merged_context;
}

}  // namespace tracing
}  // namespace datadog
