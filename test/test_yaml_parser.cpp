#include <string>

#include "test.h"
#include "yaml_parser.h"

using namespace datadog::tracing;

#define YAML_PARSER_TEST(x) TEST_CASE(x, "[yaml_parser]")

YAML_PARSER_TEST("parse empty string") {
  YamlParseResult result;
  REQUIRE(parse_yaml("", result) == YamlParseStatus::OK);
  REQUIRE(!result.config_id.has_value());
  REQUIRE(result.values.empty());
}

YAML_PARSER_TEST("parse only comments and blank lines") {
  YamlParseResult result;
  auto status = parse_yaml(
      "# This is a comment\n"
      "\n"
      "  # indented comment\n"
      "\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.empty());
}

YAML_PARSER_TEST("parse config_id at top level") {
  YamlParseResult result;
  auto status = parse_yaml("config_id: my-policy-123\n", result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.config_id.has_value());
  REQUIRE(*result.config_id == "my-policy-123");
}

YAML_PARSER_TEST("parse config_id with quotes") {
  YamlParseResult result;
  auto status = parse_yaml("config_id: \"quoted-policy-456\"\n", result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "quoted-policy-456");
}

YAML_PARSER_TEST("parse config_id with single quotes") {
  YamlParseResult result;
  auto status = parse_yaml("config_id: 'single-quoted'\n", result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "single-quoted");
}

YAML_PARSER_TEST("parse apm_configuration_default with entries") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n"
      "  DD_ENV: production\n"
      "  DD_TRACE_SAMPLE_RATE: 0.5\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.size() == 3);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
  REQUIRE(result.values.at("DD_ENV") == "production");
  REQUIRE(result.values.at("DD_TRACE_SAMPLE_RATE") == "0.5");
}

YAML_PARSER_TEST("parse with config_id and apm_configuration_default") {
  YamlParseResult result;
  auto status = parse_yaml(
      "config_id: fleet-policy-789\n"
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n"
      "  DD_VERSION: 1.2.3\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "fleet-policy-789");
  REQUIRE(result.values.size() == 2);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
  REQUIRE(result.values.at("DD_VERSION") == "1.2.3");
}

YAML_PARSER_TEST("yaml parser: duplicate keys last value wins") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE: first\n"
      "  DD_SERVICE: second\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_SERVICE") == "second");
}

YAML_PARSER_TEST("inline comments are stripped") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service  # this is a comment\n"
      "  DD_ENV: prod # env comment\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
  REQUIRE(result.values.at("DD_ENV") == "prod");
}

YAML_PARSER_TEST("quoted values preserve hash characters") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE: \"my#service\"  # comment\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_SERVICE") == "my#service");
}

YAML_PARSER_TEST("single-quoted values preserve hash characters") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE: 'my#service'  # comment\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_SERVICE") == "my#service");
}

YAML_PARSER_TEST("flow sequences are skipped") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n"
      "  DD_TAGS: [tag1, tag2]\n"
      "  DD_ENV: prod\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.count("DD_TAGS") == 0);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
  REQUIRE(result.values.at("DD_ENV") == "prod");
}

YAML_PARSER_TEST("flow mappings are skipped") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SOME_MAP: {key: value}\n"
      "  DD_SERVICE: my-service\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.count("DD_SOME_MAP") == 0);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}

YAML_PARSER_TEST("Windows line endings are handled") {
  YamlParseResult result;
  auto status = parse_yaml(
      "config_id: win-id\r\n"
      "apm_configuration_default:\r\n"
      "  DD_SERVICE: my-service\r\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "win-id");
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}

YAML_PARSER_TEST("malformed YAML returns PARSE_ERROR") {
  YamlParseResult result;
  auto status = parse_yaml("[invalid yaml: {{{", result);
  REQUIRE(status == YamlParseStatus::PARSE_ERROR);
}

YAML_PARSER_TEST("apm_configuration_default with scalar value is PARSE_ERROR") {
  YamlParseResult result;
  auto status = parse_yaml("apm_configuration_default: some_value\n", result);
  REQUIRE(status == YamlParseStatus::PARSE_ERROR);
}

YAML_PARSER_TEST("unknown top-level keys are silently ignored") {
  YamlParseResult result;
  auto status = parse_yaml(
      "some_unknown_key: some_value\n"
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}

YAML_PARSER_TEST("indented lines under unknown keys are ignored") {
  YamlParseResult result;
  auto status = parse_yaml(
      "other_section:\n"
      "  key1: val1\n"
      "  key2: val2\n"
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.size() == 1);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}

YAML_PARSER_TEST("empty values are stored") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_SERVICE:\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_SERVICE") == "");
}

YAML_PARSER_TEST("boolean-like values are stored as strings") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_TRACE_ENABLED: true\n"
      "  DD_TRACE_STARTUP_LOGS: false\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_TRACE_ENABLED") == "true");
  REQUIRE(result.values.at("DD_TRACE_STARTUP_LOGS") == "false");
}

YAML_PARSER_TEST("numeric values are stored as strings") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_TRACE_SAMPLE_RATE: 0.5\n"
      "  DD_TRACE_RATE_LIMIT: 100\n"
      "  DD_TRACE_BAGGAGE_MAX_ITEMS: 64\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_TRACE_SAMPLE_RATE") == "0.5");
  REQUIRE(result.values.at("DD_TRACE_RATE_LIMIT") == "100");
  REQUIRE(result.values.at("DD_TRACE_BAGGAGE_MAX_ITEMS") == "64");
}

YAML_PARSER_TEST("multiple top-level sections") {
  YamlParseResult result;
  auto status = parse_yaml(
      "config_id: test-id\n"
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n"
      "other_section:\n"
      "  key: value\n"
      "  another: value2\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "test-id");
  REQUIRE(result.values.size() == 1);
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}

YAML_PARSER_TEST("YAML document markers are handled") {
  YamlParseResult result;
  auto status = parse_yaml(
      "---\n"
      "config_id: doc-marker-id\n"
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n"
      "...\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "doc-marker-id");
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}

YAML_PARSER_TEST("quoted JSON strings are correctly unquoted") {
  YamlParseResult result;
  auto status = parse_yaml(
      "apm_configuration_default:\n"
      "  DD_TRACE_SAMPLING_RULES: '[{\"rate\":1}]'\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(result.values.at("DD_TRACE_SAMPLING_RULES") == "[{\"rate\":1}]");
}

YAML_PARSER_TEST("non-map root returns PARSE_ERROR") {
  YamlParseResult result;
  auto status = parse_yaml("- item1\n- item2\n", result);
  REQUIRE(status == YamlParseStatus::PARSE_ERROR);
}

YAML_PARSER_TEST("YAML anchors and aliases are handled") {
  YamlParseResult result;
  auto status = parse_yaml(
      "config_id: &id anchor-id\n"
      "apm_configuration_default:\n"
      "  DD_SERVICE: my-service\n",
      result);
  REQUIRE(status == YamlParseStatus::OK);
  REQUIRE(*result.config_id == "anchor-id");
  REQUIRE(result.values.at("DD_SERVICE") == "my-service");
}
