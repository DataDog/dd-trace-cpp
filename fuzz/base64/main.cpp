#include <datadog/base64.h>
#include <datadog/string_view.h>

namespace dd = datadog::tracing;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, size_t size) {
  dd::base64::decode(dd::StringView{(char*)data, size});
  return 0;
}
