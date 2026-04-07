#include <datadog/runtime_id.h>

#include "../test.h"
#include "root_session_id.h"

using namespace datadog::tracing;

#define ROOT_SESSION_ID_TEST(x) \
  TEST_CASE(x, "[telemetry],[telemetry.root_session_id]")

ROOT_SESSION_ID_TEST("get_or_init returns the first runtime ID") {
  auto first_rid = RuntimeID::generate();
  auto second_rid = RuntimeID::generate();

  const auto& result1 = root_session_id::get_or_init(first_rid.string());
  const auto& result2 = root_session_id::get_or_init(second_rid.string());

  CHECK(result1 == first_rid.string());
  CHECK(result2 == first_rid.string());
  CHECK(result1 == result2);
}
