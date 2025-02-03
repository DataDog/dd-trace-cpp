#include <datadog/baggage.h>

#include "catch.hpp"
#include "mocks/dict_readers.h"
#include "mocks/dict_writers.h"

#define BAGGAGE_TEST(x) TEST_CASE(x, "[baggage]")

using namespace datadog::tracing;

BAGGAGE_TEST("missing baggage header is not an error") {
  MockDictReader reader;
  auto maybe_baggage = Baggage::extract(reader);
  CHECK(!maybe_baggage);
}

BAGGAGE_TEST("extract") {
  SECTION("limit is respected") {
    const std::unordered_map<std::string, std::string> headers{
        {"baggage", "team=proxy,company=datadog,user=dmehala"}};
    MockDictReader reader(headers);

    auto extraction_fails = Baggage::extract(reader, 1);
    REQUIRE(!extraction_fails);
    CHECK(extraction_fails.error() == Baggage::Error::MAXIMUM_CAPACITY_REACHED);
  }

  SECTION("parsing") {
    struct TestCase final {
      std::string name;
      std::string input;
      Expected<Baggage, Baggage::Error> expected_baggage;
    };

    auto test_case = GENERATE(values<TestCase>({
        {
            "empty baggage header",
            "",
            Baggage(),
        },
        {
            "only spaces",
            "                  ",
            Baggage::Error::MALFORMED_BAGGAGE_HEADER,
        },
        {
            "valid",
            "key1=value1,key2=value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 1",
            "    key1=value1,key2=value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 2",
            "    key1    =value1,key2=value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 3",
            "    key1    = value1,key2=value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 4",
            "    key1    = value1  ,key2=value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 5",
            "    key1    = value1  , key2=value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 6",
            "    key1    = value1  , key2  =value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 7",
            "    key1    = value1  , key2  =   value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 8",
            "    key1    = value1  , key2  =   value2  ",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "leading spaces 9",
            "key1   = value1,   key2=   value2",
            Baggage({{"key1", "value1"}, {"key2", "value2"}}),
        },
        {
            "spaces in key is allowed",
            "key1 foo=value1",
            Baggage({{"key1 foo", "value1"}}),
        },
        {
            "verify separator",
            "key1=value1;a=b,key2=value2",
            Baggage({{"key1", "value1;a=b"}, {"key2", "value2"}}),
        },
        {
            "malformed baggage",
            ",k1=v1,k2=v2,",
            Baggage::Error::MALFORMED_BAGGAGE_HEADER,
        },
        {
            "malformed baggage 2",
            "=",
            Baggage::Error::MALFORMED_BAGGAGE_HEADER,
        },
        {
            "malformed baggage 3",
            "=,key2=value2",
            Baggage::Error::MALFORMED_BAGGAGE_HEADER,
        },
        {
            "malformed baggage 4",
            "key1=value1,=",
            Baggage::Error::MALFORMED_BAGGAGE_HEADER,
        },

    }));

    CAPTURE(test_case.name, test_case.input);

    const std::unordered_map<std::string, std::string> headers{
        {"baggage", test_case.input}};
    MockDictReader reader(headers);

    auto maybe_baggage = Baggage::extract(reader);
    if (maybe_baggage.has_value() && test_case.expected_baggage.has_value()) {
      CHECK(*maybe_baggage == *test_case.expected_baggage);
    } else if (maybe_baggage.if_error() &&
               test_case.expected_baggage.if_error()) {
      CHECK(maybe_baggage.error() == test_case.expected_baggage.error());
    } else {
      FAIL("mistmatch between what is expected and the result");
    }
  }
}

BAGGAGE_TEST("inject") {
  SECTION("limit is respected") {
    Baggage bag({{"violets", "blue"}, {"roses", "red"}});

    MockDictWriter writer;
    auto injected = bag.inject(writer, 5);
    REQUIRE(!injected);
    CHECK(injected.error().code == Error::Code::BAGGAGE_MAXIMUM_BYTES_REACHED);
  }
}

BAGGAGE_TEST("round-trip") {
  Baggage bag({
      {"team", "proxy"},
      {"company", "datadog"},
      {"user", "dmehala"},
  });

  MockDictWriter writer;
  REQUIRE(bag.inject(writer, 2048));

  MockDictReader reader(writer.items);
  auto extracted_baggage = Baggage::extract(reader);
  REQUIRE(extracted_baggage);

  CHECK(*extracted_baggage == bag);
}

BAGGAGE_TEST("accessors") {
  Baggage bag({{"foo", "bar"}, {"answer", "42"}, {"dog", "woof"}});

  const std::string baggage = "team=proxy,company=datadog,user=dmehala";
  const std::unordered_map<std::string, std::string> headers{
      {"baggage", baggage}};
  MockDictReader reader(headers);

  auto maybe_baggage = Baggage::extract(reader);
  REQUIRE(maybe_baggage);

  CHECK(maybe_baggage->size() == 3);

  CHECK(maybe_baggage->get("company") == "datadog");
  CHECK(!maybe_baggage->get("boogaloo"));
  CHECK(maybe_baggage->contains("boogaloo") == false);
  CHECK(maybe_baggage->contains("team") == true);

  maybe_baggage->set("color", "red");
  /// NOTE: assure `set` overwrite
  maybe_baggage->set("color", "blue");
  CHECK(maybe_baggage->get("color") == "blue");
  CHECK(maybe_baggage->size() == 4);

  maybe_baggage->remove("company");
  CHECK(maybe_baggage->contains("company") == false);
  CHECK(maybe_baggage->size() == 3);

  SECTION("visit") {
    bag.visit([](StringView key, StringView value) {
      (void)key;
      (void)value;
    });
  }

  SECTION("clear") {
    bag.clear();
    CHECK(bag.empty() == true);
  }
}
