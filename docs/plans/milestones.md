# Milestones

This file sets the order in which the emulator described in [architecture.md](../architecture.md) is
built. Each entry is one milestone, made of session-sized tasks.

**This file only shrinks.** A milestone is removed in the commit that completes it. A task that
needs a decision the records do not hold stops, and puts the question in
[open-questions.md](../open-questions.md) instead of deciding it.

A session takes work by saying *build M<n> of docs/plans/milestones.md*. Every task ends with:

- the suite green;
- `./scripts/format.sh --check` and `./scripts/tidy.sh` clean;
- documentation updated in the same commit;
- no commit until the owner has reviewed.

Every milestone ends with an **exit** condition that can be checked without a person looking at
a screen.

## M8: Regression and release

- Batch scripts with checkpoints summing the picture, memory and sound, and a regression suite
  of them (`ctest -L regression`).
- The project skill, `.claude/skills/petmcp/SKILL.md`, for driving the emulator from an agent.
- CI green on three platforms; a tagged release.

**Exit:** CI publishes a release from a tag.
