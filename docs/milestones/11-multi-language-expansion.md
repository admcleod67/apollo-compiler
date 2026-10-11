← [Project milestones index](../milestones.md)

## Milestone 11 — Multi-language expansion (deferred)

**Status: deferred.** Apollo prioritises Pascal through
[Milestone 7 — language completeness](07-pascal-language-completeness.md),
[Milestone 8 — Pascal runtime library](08-pascal-runtime-library.md),
[Milestone 9 — Pick database API](09-pick-database-api.md), and optional
[Milestone 10 — dialect polish](10-pascal-dialect-polish.md) before additional source
languages.

### Intended scope (when resumed)

- BASIC front-end
- COMAL front-end
- Fortran front-end
- COBOL front-end

Front-ends share the IR and Gemini backend established in Milestones 4–5. Pascal remains
the reference language for IR and codegen contracts until the Pascal + Pick runtime track
is far enough for teaching and Gemini Pick applications.

### Why deferred

- A single deep Pascal dialect plus a usable runtime surface (console, Wirth files, PickDB)
  validates IR, lowering, and `.tbc` emission under real language pressure.
- Spreading effort across incomplete front-ends would dilute Pascal completeness without
  delivering a usable second language.
- Gemini already hosts an in-tree BASIC path; Apollo’s next value is a credible Pascal
  compiler for Gemini (including Pick), not a second BASIC.

Resume this milestone only after Milestones 8–9 success criteria are met (or explicitly
re-scoped), and optionally after Milestone 10 if polish was in flight. Milestone 7 remains
the dialect baseline.
