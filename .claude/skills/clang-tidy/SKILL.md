---
name: clang-tidy
description: >-
  Enable one clang-tidy check in the blocking tidy CI job.
  Use when introducing a mechanically enforceable C++ rule.
allowed-tools: Bash Read Grep Glob Edit
---

# Add a clang-tidy check

Grow the blocking tidy job **one check at a time**. Do not turn this
into a style-guide dump or a clang-format gate.

Background: [docs/development.md](../../../docs/development.md),
[`.clang-tidy`](../../../.clang-tidy), [`bin/check-tidy`](../../../bin/check-tidy).

## This repository

- First-party TUs: `src/`
- Headers that may report: `src/` and `include/`
- Run: `bin/check-tidy` (Docker; never a host `compile_commands.json`)
- CI: `verify` runs the same script

Leave `binding/`, `examples/`, `fuzz/`, `test/`, `tools/`, and vendored
trees out unless the user explicitly expands the path filter.

## Hard constraints

- Enable **one** check per change. Prefer uncommenting a deferred line
  in `.clang-tidy` over adding a new check name.
- Do not enable a check group (`bugprone-*`, `readability-*`, …).
- Keep `WarningsAsErrors: '*'`.
- Do not add `CXX_CLANG_TIDY` / `CMAKE_CXX_CLANG_TIDY` to CMake.
- Do not pass `-extra-arg` to clang-tidy. The compile database from
  `ci-clang` is the compile line.
- Do not run tidy on the host. `bin/check-tidy` re-execs in the CI image.
- Do not mark the tidy step `allow_failure`. The job is meant to block.
- Do not widen `-header-filter` or the `src/` TU regex unless asked.
- Keep the shared check list comment at the top of `.clang-tidy` accurate
  if you change enabled or deferred checks.

## Workflow

1. Read `.clang-tidy`. If the rule is already listed under
   “Deferred until existing findings are cleaned up”, uncomment that
   one line and add it to `Checks`.
2. Otherwise add **one** check name to `Checks` (after `-*`).
3. Run `bin/check-tidy` from the repo root.
4. If it fails:
   - Mechanical first-party fixes in `src/` / `include/` are OK in the
     same change (`std::forward` instead of `std::move` on a forwarding
     reference, drop a redundant `(void)`, and so on).
   - Do not edit vendored or generated files to silence tidy.
   - If the cleanup is large or opinionated, comment the check back out
     (name the failing files) and stop. Ask before a repo-wide rewrite.
5. Keep the check only if `bin/check-tidy` exits 0.

## Local command

```bash
bin/check-tidy
```
