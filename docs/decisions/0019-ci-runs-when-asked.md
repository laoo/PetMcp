# 0019: CI runs when asked, and on a version tag

**Status:** accepted

## Context

[0013](0013-ci-and-releases.md) has CI build and test the three platforms on every push and pull
request. The milestones that built the emulator are done, and its plan is gone; the owner pushes
work as it is reviewed, and three platforms' builds on every push spend minutes no one reads.

## Decision

- `.github/workflows/ci.yml` runs when started by hand (`workflow_dispatch`) and when a tag `v*`
  is pushed. A push of a branch, and a pull request, start nothing.
- What a run does is unchanged: the three platforms built with warnings as errors, the suite, the
  formatting, the packages; a tag publishes them as a release.

## Consequences

- A change that breaks a platform is found when CI is next run, by hand before a release or by
  the tag itself, not when it is pushed. Run it by hand before tagging.
- A tag whose run fails publishes nothing; the tag stays, and a fixed commit takes the next one.
