#include <cmath>
#include <string>

#include "ddsketch.h"
#include "test.h"

using namespace datadog::tracing;

#define DDSKETCH_TEST(x) TEST_CASE(x, "[ddsketch]")

DDSKETCH_TEST("empty sketch") {
  DDSketch sketch;

  CHECK(sketch.empty());
  CHECK(sketch.count() == 0);
  CHECK(sketch.sum() == 0.0);
  CHECK(sketch.min() == 0.0);
  CHECK(sketch.max() == 0.0);
  CHECK(sketch.avg() == 0.0);
}

DDSKETCH_TEST("single value") {
  DDSketch sketch;
  sketch.add(100.0);

  CHECK(!sketch.empty());
  CHECK(sketch.count() == 1);
  CHECK(sketch.sum() == 100.0);
  CHECK(sketch.min() == 100.0);
  CHECK(sketch.max() == 100.0);
  CHECK(sketch.avg() == 100.0);
}

DDSKETCH_TEST("multiple values") {
  DDSketch sketch;
  sketch.add(10.0);
  sketch.add(20.0);
  sketch.add(30.0);

  CHECK(sketch.count() == 3);
  CHECK(sketch.sum() == Approx(60.0));
  CHECK(sketch.min() == Approx(10.0));
  CHECK(sketch.max() == Approx(30.0));
  CHECK(sketch.avg() == Approx(20.0));
}

DDSKETCH_TEST("zero value goes to zero bucket") {
  DDSketch sketch;
  sketch.add(0.0);

  CHECK(sketch.count() == 1);
  CHECK(sketch.sum() == 0.0);
  CHECK(sketch.min() == 0.0);
  CHECK(sketch.max() == 0.0);
}

DDSKETCH_TEST("negative value treated as zero") {
  DDSketch sketch;
  sketch.add(-5.0);

  CHECK(sketch.count() == 1);
  CHECK(sketch.sum() == 0.0);
  CHECK(sketch.min() == 0.0);
  CHECK(sketch.max() == 0.0);
}

DDSKETCH_TEST("very small positive value goes to zero bucket") {
  DDSketch sketch;
  sketch.add(1e-12);  // below the min_positive threshold

  CHECK(sketch.count() == 1);
  CHECK(sketch.sum() == Approx(1e-12));
}

DDSKETCH_TEST("large number of values") {
  DDSketch sketch;
  double total = 0.0;
  for (int i = 1; i <= 1000; ++i) {
    double val = static_cast<double>(i);
    sketch.add(val);
    total += val;
  }

  CHECK(sketch.count() == 1000);
  CHECK(sketch.sum() == Approx(total));
  CHECK(sketch.min() == Approx(1.0));
  CHECK(sketch.max() == Approx(1000.0));
  CHECK(sketch.avg() == Approx(500.5));
}

DDSKETCH_TEST("clear resets the sketch") {
  DDSketch sketch;
  sketch.add(100.0);
  sketch.add(200.0);

  sketch.clear();

  CHECK(sketch.empty());
  CHECK(sketch.count() == 0);
  CHECK(sketch.sum() == 0.0);
}

DDSKETCH_TEST("msgpack_encode produces non-empty output") {
  DDSketch sketch;
  sketch.add(1000000.0);   // 1ms in nanoseconds
  sketch.add(5000000.0);   // 5ms
  sketch.add(10000000.0);  // 10ms

  std::string encoded;
  sketch.msgpack_encode(encoded);

  // The output should be non-empty.
  CHECK(!encoded.empty());
  // It should start with a msgpack map marker.
  CHECK(static_cast<unsigned char>(encoded[0]) == 0xDF);  // MAP32
}

DDSKETCH_TEST("msgpack_encode of empty sketch") {
  DDSketch sketch;

  std::string encoded;
  sketch.msgpack_encode(encoded);

  // Even an empty sketch should produce a valid encoding.
  CHECK(!encoded.empty());
}

DDSKETCH_TEST("custom relative accuracy") {
  DDSketch sketch(0.05, 1024);  // 5% accuracy, 1024 bins

  sketch.add(100.0);
  sketch.add(200.0);

  CHECK(sketch.count() == 2);
  CHECK(sketch.sum() == Approx(300.0));
}
