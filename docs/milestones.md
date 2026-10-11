# Project milestones

This document is an **orienting roadmap**. Priorities and scope may change as Apollo and
the Gemini System evolve.

**Current priority:** [Milestone 8 — Pascal runtime library](milestones/08-pascal-runtime-library.md)
(finish Wirth file I/O / Stage 3 when the host file-service façade is ready). Next:
[Milestone 9 — Pick database API](milestones/09-pick-database-api.md) for Gemini Pick.
Optional dialect polish is [Milestone 10](milestones/10-pascal-dialect-polish.md). Further
source languages remain deferred as
[Milestone 11](milestones/11-multi-language-expansion.md).

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
- [Milestone 7 — Pascal language completeness (completed)](milestones/07-pascal-language-completeness.md)
- [Milestone 8 — Pascal runtime library](milestones/08-pascal-runtime-library.md)
- [Milestone 9 — Pick database API](milestones/09-pick-database-api.md)
- [Milestone 10 — Pascal dialect polish (optional)](milestones/10-pascal-dialect-polish.md)
- [Milestone 11 — Multi-language expansion (deferred)](milestones/11-multi-language-expansion.md)

**Release checkpoint:** Milestone 7 is complete. The toolchain reports `0.7.0`; cut
git tag `v0.7.0` when ready. Active Pascal runtime work is Milestone 8; Pick integration
is Milestone 9. Multi-language expansion remains deferred as Milestone 11.
