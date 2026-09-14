← [Project milestones index](../milestones.md)

## Milestone 8 — Multi-language expansion (deferred)

**Status: deferred.** Apollo prioritises [Milestone 7 — Pascal language completeness](07-pascal-language-completeness.md)
(Wirth console / Pascal80-style) before additional source languages.

### Intended scope (when resumed)

- BASIC front-end
- COMAL front-end
- Fortran front-end
- COBOL front-end

Front-ends share the IR and Gemini backend established in Milestones 4–5. Pascal remains
the reference language for IR and codegen contracts until M7 closes.

### Why deferred

- A single deep Pascal dialect validates IR, lowering, and `.tbc` emission under real
  language pressure.
- Spreading effort across incomplete front-ends would dilute Milestone 7’s completeness
  goals without delivering a usable second language.
- Gemini already hosts an in-tree BASIC path; Apollo’s next value is a credible Pascal
  console compiler, not a second BASIC.

Resume this milestone only after M7’s success criteria are met (or explicitly re-scoped).
