# dd-trace-cpp

## Build System

This is a CMake project. The default build directory is `.build`.

### Building

```bash
cmake -B .build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDD_TRACE_BUILD_TESTING=ON
cmake --build .build -j
```

### Running Tests

```bash
ctest --test-dir .build
```

### When builds fail after source changes

**Do NOT use `rm -rf` on build directories.** Instead, use these approaches in order:

1. **Reconfigure** (fixes most cache staleness issues):
   ```bash
   cmake -B .build
   ```

2. **Clean build artifacts only** (preserves fetched dependencies):
   ```bash
   cmake --build .build --target clean
   cmake --build .build -j
   ```

3. **Delete CMake cache only** (forces full reconfigure without re-downloading deps):
   ```bash
   rm .build/CMakeCache.txt
   cmake -B .build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDD_TRACE_BUILD_TESTING=ON
   cmake --build .build -j
   ```

Only as an absolute last resort, if none of the above work, remove the build directory entirely. This re-downloads all FetchContent dependencies (curl, nlohmann_json, yaml-cpp, etc.) and is slow.
