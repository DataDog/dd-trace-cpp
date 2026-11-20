# Adding or Changing a Configuration

This guide explains how to add, rename or change a configuration (an environment variable) in
`dd-trace-cpp`. Follow every step. CI checks parts of steps 2 to 4.

## Overview

- All configurations are listed in one place: `DD_LIST_ENVIRONMENT_VARIABLES` in
  [include/datadog/environment.h](../include/datadog/environment.h).
- The `config-inversion` tool ([tools/config-inversion](../tools/config-inversion)) reads that list
  and writes [supported-configurations.json](../supported-configurations.json) at the repo root.
- The JSON file is committed. CI checks that it matches the list.
- CI also checks every key in the JSON file against the central configuration registry (the
  [Feature Parity Dashboard](https://feature-parity.us1.prod.dog/#/configurations?viewType=configurations)).

## 1. Check the Central Registry

Search the registry for the setting before you pick a name. If another tracer already has it, reuse
its name and behavior. If the key is new for all tracers, add it to the registry.

Each registry entry has one or more implementations, named `A`, `B`, `C`, and so on. Each one has a
type, a default and a description. The registry description is what users see: the public
[C++ configuration page](https://docs.datadoghq.com/tracing/trace_collection/library_config/cpp/)
builds its list of keys from the registry.

## 2. Add the Entry

Add one line at the end of `DD_LIST_ENVIRONMENT_VARIABLES`. Add a `\` to the line that was last.
The new last line has no `\`:

```cpp
  MACRO(DD_EXTERNAL_ENV, STRING, nullptr)                                      \
  MACRO(DD_MY_NEW_SETTING, BOOLEAN, false)
```

- `NAME`: the environment variable name. It also becomes the `environment::Variable` enum value.
- `TYPE`: one of `STRING`, `BOOLEAN`, `INT`, `DECIMAL`, `ARRAY`, `MAP`.
- `DEFAULT`: the default value. Use:
  - a literal (`"localhost"`, `true`, `8126`, `5.0`) when the default is fixed;
  - `nullptr` when there is no default (written as JSON `null`);
  - `ENV_DEFAULT_RESOLVED_IN_CODE("...")` when the code computes the default at runtime. For
    example, `DD_SERVICE` defaults to the process name. The JSON `default` is then an empty string.
    The text is only for readers of the header. Do not use it with `DECIMAL`: the tool would write
    `""` (two quote characters) as the default.

The type and default must match the registry implementation you use. The JSON keys are sorted, so
the order does not matter for the JSON. Add new entries at the end, as shown above: the list
also defines the public `environment::Variable` enum, and the existing values must not change.

Run `bin/format` after editing the header. CI runs `bin/check-format`.

## 3. Read the Value in Code

Never call `std::getenv`. CI runs `bin/check-environment-variables` and fails if `std::getenv` is
used outside `src/datadog/environment.cpp` and `src/datadog/curl.cpp`. Use `environment::lookup`.

Follow the `DD_SERVICE` pattern:

1. Add a field to the public config struct, for example `TracerConfig` in
   [include/datadog/tracer_config.h](../include/datadog/tracer_config.h). Note the environment
   variable in the field comment. This is the user-facing doc for code users.
2. Read the variable in the `load_*_env_config` function of the same component, for example
   `load_tracer_env_config` in [src/datadog/tracer_config.cpp](../src/datadog/tracer_config.cpp):

   ```cpp
   if (auto service_env = lookup(environment::DD_SERVICE)) {
     env_cfg.service = std::string{*service_env};
   }
   ```

   Parse the text with the helpers in [src/datadog/parse_util.h](../src/datadog/parse_util.h)
   (`falsy`, `parse_uint64`, `parse_double`, `parse_list`, `parse_tags`). On a parse error, return
   an `Error` that names the variable with `name(environment::...)`.
3. Add a `ConfigName` value at the end of the enum in
   [include/datadog/config.h](../include/datadog/config.h), so existing values keep their
   numbers. Map it to
   its telemetry name in `to_string` in
   [src/datadog/telemetry/telemetry_impl.cpp](../src/datadog/telemetry/telemetry_impl.cpp).
4. Pick the final value in `finalize_config` with `resolve_and_record_config` from `config.h`. It
   applies the precedence environment > code > default, and records the origin for telemetry:

   ```cpp
   final_config.defaults.service = resolve_and_record_config(
       env_config->service, user_config.service, &final_config.metadata,
       ConfigName::SERVICE_NAME, get_process_name());
   ```

   For a value that is not a string, pass a function that turns it into a string as the last
   argument. See the `log_on_startup` call in `tracer_config.cpp`.

Other components in `src/datadog` also have a `load_*_env_config` function. Some of them pick the
final value differently:

- `span_sampler_config.cpp` uses `resolve_and_record_config`.
- `trace_sampler_config.cpp` uses `resolve_and_record_config`, and sets the sampling rules metadata
  by hand.
- `datadog_agent_config.cpp` mostly uses `value_or`. It uses `pick` for the agent URL and sets the
  `AGENT_URL` metadata by hand.
- `telemetry/configuration.cpp` uses `pick` and records no `ConfigName` metadata. Do not add a
  `ConfigName` there.

Follow the code next to your setting.

## 4. Regenerate supported-configurations.json

Build the tool and run it from the repo root:

```shell
cmake -B .build -DDD_TRACE_BUILD_TOOLS=1 .
cmake --build .build -j --target config-inversion
./.build/tools/config-inversion/config-inversion --output-file supported-configurations.json
```

Run it from the repo root. The tool reads the existing `supported-configurations.json` in the
current directory to keep each key's `implementation`. In any other directory it aborts with
`parse error ... attempting to parse an empty input`.

New keys get implementation `A`. If the registry uses another letter for this behavior, edit only
that `implementation` value in the JSON file, then run the tool again. The tool keeps it.

Commit the updated `supported-configurations.json` with the header change. Do not edit other fields
by hand.

## 5. Add Tests

Use `EnvGuard` from [test/common/environment.h](../test/common/environment.h). It sets a variable
for one scope and restores it after.

- Tracer, agent and sampler settings: [test/test_tracer_config.cpp](../test/test_tracer_config.cpp).
  See the `DD_SERVICE overrides service` and `DD_TRACE_RATE_LIMIT` sections.
- Telemetry settings: [test/telemetry/test_configuration.cpp](../test/telemetry/test_configuration.cpp).

Test at least: the default, the environment value overriding the code value, and an invalid value.
If you record metadata, check its origin and value (see the `PROPAGATION_BEHAVIOR_EXTRACT` tests).
See [development.md](development.md) to run the tests.

## Renaming or Removing a Configuration

The `config-inversion` tool does not emit aliases or deprecations. To rename, add the new
name as its own entry and keep the old one. Read both in code and decide which wins. See how
`DD_TRACE_PROPAGATION_STYLE_EXTRACT` and `DD_PROPAGATION_STYLE_EXTRACT` are handled in
`load_tracer_env_config`. Removing an entry removes the key from the JSON file. Users of that key
then lose support, so treat it as a breaking change.

## Troubleshooting

### GitHub: "supported-configurations.json got out of sync"

The `Verify supported configurations metadata` step in
[.github/workflows/dev.yml](../.github/workflows/dev.yml) builds `config-inversion`, runs it, and
diffs the output with the committed file. To fix it, run step 4 and commit the result.

The tool writes no final newline. If your editor adds one, the diff fails. Regenerate the file
instead of editing it.

### GitLab: `validate_supported_configurations_v2_local_file`

This job in [.gitlab-ci.yml](../.gitlab-ci.yml) sends `supported-configurations.json` to the
central registry. It fails when:

- the key is not in the registry: add it to the registry;
- the `implementation` is not in the registry: use an existing letter, or add a new
  implementation in the registry;
- the type or default does not match the registry implementation: fix the code and the header
  entry to match. If another implementation matches, the job log lists it. Use that letter.

Do not change the local values just to pass the check. They must describe what the code does.

The `update_central_configurations_version_range_v2` job runs only on release tags. It records
which versions support each key. It needs no action in a PR.
