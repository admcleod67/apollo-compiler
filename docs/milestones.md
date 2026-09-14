# Project milestones

This document is an **orienting roadmap**. Priorities and scope may change as Apollo and
the Gemini System evolve.

**Current priority:** deepen Pascal toward a Wirth console / Pascal80-style dialect
([Milestone 7](milestones/07-pascal-language-completeness.md)) before adding further
source languages ([Milestone 8](milestones/08-multi-language-expansion.md), deferred).

## Milestone index

Detailed scope for each milestone is in [`docs/milestones/`](milestones/) (two-digit
prefixes sort lexicographically).

- [Milestone 0 — Project skeleton (completed)](milestones/00-project-skeleton.md)
- [Milestone 1 — Source & scanner infrastructure (completed)](milestones/01-source-and-scanner-infrastructure.md)
- [Milestone 2 — Parser (completed)](milestones/02-parser.md)
- [Milestone 3 — Semantic analysis (completed)](milestones/03-semantic-analysis.md)
- [Milestone 4 — Intermediate representation (completed)](milestones/04-intermediate-representation.md)
- [Milestone 5 — Code generator (completed)](milestones/05-code-generator.md)
- [Milestone 6 — Standalone VM execution (completed)](milestones/06-standalone-vm-execution.md)
- [Milestone 7 — Pascal language completeness](milestones/07-pascal-language-completeness.md)
- [Milestone 8 — Multi-language expansion (deferred)](milestones/08-multi-language-expansion.md)

**Release checkpoint:** Milestone 6 is complete. The toolchain reports `0.6.0`; cut
git tag `v0.6.0` when ready. Milestone 7 is next (Wirth console / Pascal80-style
Pascal completeness). Multi-language expansion remains deferred as Milestone 8.
