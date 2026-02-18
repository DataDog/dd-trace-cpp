# App-Extended-Heartbeat Implementation Summary

## Overview
Successfully implemented the `app-extended-heartbeat` telemetry event for the C++ tracer library. This event serves as a failsafe for catastrophic data failures and helps reconstruct application records.

## Implementation Details

### 1. Environment Variable Configuration
**File**: `include/datadog/environment.h`
- Added `DD_TELEMETRY_EXTENDED_HEARTBEAT_INTERVAL` to the environment variable registry
- Positioned after `DD_TELEMETRY_HEARTBEAT_INTERVAL` for logical grouping

### 2. Configuration Structures
**Files**: `include/datadog/telemetry/configuration.h`
- Added `extended_heartbeat_interval_seconds` (Optional<double>) to `Configuration` struct
- Added `extended_heartbeat_interval` (chrono::steady_clock::duration) to `FinalizedConfiguration` struct

### 3. Configuration Parsing
**File**: `src/datadog/telemetry/configuration.cpp`
- Added parsing logic for `DD_TELEMETRY_EXTENDED_HEARTBEAT_INTERVAL` environment variable
- Default value: **86400 seconds (24 hours)**
- Added validation to ensure interval is positive
- Follows same pattern as existing heartbeat interval configuration

### 4. Telemetry Implementation
**Files**: `src/datadog/telemetry/telemetry_impl.{h,cpp}`

#### Header Changes (telemetry_impl.h:156-159)
- Added `app_extended_heartbeat_payload()` method declaration

#### Implementation Changes (telemetry_impl.cpp)

**Payload Generation (lines 682-715)**:
```cpp
std::string Telemetry::app_extended_heartbeat_payload()
```
- Constructs payload with three arrays:
  - `configuration`: Reuses configuration from products (same as app-started)
  - `dependencies`: Empty array (placeholder for future use)
  - `integrations`: Contains integration name/version if configured
- **Excludes** `products` and `install_signature` fields (per API requirements)
- Uses existing `generate_configuration_field()` helper for consistency

**Task Scheduling (lines 221-230)**:
- Added recurring event scheduling for extended heartbeat
- Scheduled alongside regular heartbeat and metrics capture
- Uses configured interval from `config_.extended_heartbeat_interval`

### 5. Testing
**File**: `test/telemetry/test_telemetry.cpp`

#### FakeEventScheduler Updates (lines 45-83)
- Added `extended_heartbeat_callback` member
- Added `extended_heartbeat_interval` member
- Updated task counting logic (heartbeat=0, extended=1, metrics=2)
- Added `trigger_extended_heartbeat()` method

#### New Test Section (lines 394-452)
- Test name: "generates an extended heartbeat message"
- Validates:
  - ✅ Payload has correct request_type: `app-extended-heartbeat`
  - ✅ Contains required fields: `configuration`, `dependencies`, `integrations`
  - ✅ All fields are arrays
  - ✅ Does NOT contain `products` or `install_signature`
  - ✅ Configuration data is correctly formatted with seq_id

#### Updated Interval Test (lines 950-968)
- Added validation for extended heartbeat callback
- Verifies default interval is 86400 seconds (24 hours)
- Fixed existing bug: line 964 now correctly checks `heartbeat_interval` instead of `metrics_interval`

## API Compliance

### Payload Structure
Follows the API specification from:
`/Users/ayan.khan/Code/instrumentation-telemetry-api-docs/.../app_extended_heartbeat.md`

**Included Fields**:
- ✅ `configuration` (array of conf_key_value)
- ✅ `dependencies` (array of dependency) - empty for now
- ✅ `integrations` (array of integration)

**Excluded Fields** (as required):
- ❌ `products` - not included
- ❌ `install_signature` - not included

## Configuration

### Environment Variable
```bash
DD_TELEMETRY_EXTENDED_HEARTBEAT_INTERVAL=86400
```
- Type: Double (seconds)
- Default: 86400 (24 hours)
- Must be positive value

### Code Configuration
```cpp
Configuration cfg;
cfg.extended_heartbeat_interval_seconds = 3600.0; // 1 hour
```

## Event Flow

1. **Initialization**: `Telemetry` constructor schedules three recurring events:
   - Regular heartbeat (default: 10s)
   - Extended heartbeat (default: 24h)
   - Metrics capture (default: 60s, if enabled)

2. **Execution**: Every 24 hours (by default):
   - `app_extended_heartbeat_payload()` generates payload
   - `send_payload()` sends HTTP POST to agent
   - Telemetry endpoint: `/telemetry/proxy/api/v2/apmtelemetry`
   - Request type header: `app-extended-heartbeat`

3. **Payload Content**:
   - Configuration from all products
   - Empty dependencies array
   - Integration info (if configured)

## Files Modified

1. `include/datadog/environment.h` - Added env var
2. `include/datadog/telemetry/configuration.h` - Added config structs
3. `src/datadog/telemetry/configuration.cpp` - Added parsing logic
4. `src/datadog/telemetry/telemetry_impl.h` - Added method declaration
5. `src/datadog/telemetry/telemetry_impl.cpp` - Added implementation
6. `test/telemetry/test_telemetry.cpp` - Added tests

## Build & Test Commands

```bash
# Using Bazel
bazel test //test/telemetry:test_telemetry

# Using CMake
bin/cmake-build
cd .build
ctest -R telemetry
```

## Verification Checklist

- ✅ Environment variable `DD_TELEMETRY_EXTENDED_HEARTBEAT_INTERVAL` works
- ✅ Default is 24 hours (86400 seconds)
- ✅ Custom intervals can be configured
- ✅ Event scheduled at correct interval
- ✅ Payload contains: configuration, dependencies, integrations
- ✅ Payload does NOT contain: products, install_signature
- ✅ Test coverage added
- ✅ Follows existing code patterns

## Future Enhancements

1. **Dependencies Population**: Currently returns empty array. Could be enhanced to:
   - List loaded libraries
   - Include library versions
   - Track runtime dependencies

2. **Integration Auto-detection**: Currently uses configured integration. Could be enhanced to:
   - Auto-detect available integrations
   - Report integration status (enabled/disabled)
   - Include compatibility information

## Notes

- Implementation reuses existing `app-started` payload generation logic for configuration
- Maintains consistency with other telemetry events (heartbeat, app-started, app-closing)
- Uses standard telemetry message format with seq_id tracking
- Thread-safe (uses existing mutex patterns from Telemetry class)
- Minimal overhead - only executes once per day by default
