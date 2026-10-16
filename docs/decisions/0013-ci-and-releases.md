# 0013: CI builds three platforms, and a tag makes a release

**Status:** accepted

## Context

PGMEmu builds and tests on macOS, Linux and Windows on every push, and publishes a release from
a tag (its record 0022). The workflow is taken unchanged.

## Decision

- `.github/workflows/ci.yml` builds and tests on macOS arm64, Linux x64 and Windows x64, with
  warnings as errors (`PET_WERROR`), and checks the formatting.
- The tests that need the PET's ROMs skip there: no ROM is part of the repository, and none is
  fetched. They are run where the ROMs are, before a release.
- A tag `v*` publishes each platform's package, what `cmake --install <build> --component petmcp`
  holds.

## Consequences

- A change that builds on one platform and not another fails CI before it is merged.
- clang-tidy stays a local step.
