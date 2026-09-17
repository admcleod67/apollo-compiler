← [Project milestones index](../milestones.md)

## Milestone 9 — Multi-language expansion (deferred)

**Status: deferred.** Apollo prioritises Pascal through
[Milestone 7 — language completeness](07-pascal-language-completeness.md) and
[Milestone 8 — Pascal runtime library](08-pascal-runtime-library.md) before additional
source languages.

### Intended scope (when resumed)

- BASIC front-end
- COMAL front-end
- Fortran front-end
- COBOL front-end

Front-ends share the IR and Gemini backend established in Milestones 4–5. Pascal remains
the reference language for IR and codegen contracts until the Pascal runtime track is
far enough for teaching and small console programs.

### Why deferred

- A single deep Pascal dialect plus a usable runtime surface validates IR, lowering, and
  `.tbc` emission under real language pressure.
- Spreading effort across incomplete front-ends would dilute Pascal completeness without
  delivering a usable second language.
- Gemini already hosts an in-tree BASIC path; Apollo’s next value is a credible Pascal
  console compiler (language + library), not a second BASIC.

Resume this milestone only after Milestone 8’s success criteria are met (or explicitly
re-scoped), and Milestone 7 remains the dialect baseline.
