← [Project milestones index](../milestones.md)

## Milestone 4 — Intermediate representation

- Language-neutral IR design
- IR generator (from Pascal semantic AST)
- Control flow representation

### Open debts / prerequisites (from M3)

**Recursive array assignability**

Today `isAssignable` in `src/pascal/semantic/Analyse.cpp` peels aliases via
`canonicalTag` and then compares tags only. Two values with `TypeTag::Array` therefore
count as assignable even when their element types differ (e.g. `array of integer` vs
`array of real`).

Before or while lowering array-typed values in Milestone 4, tighten assignability:

1. Peel aliases as today.
2. If both sides have tag `Array`, require the **element** types to be assignable (or
   identical) via recursion on `Type::element`.
3. Static index-bound equality can wait until bounds matter for IR layout / sizing.

Touch points: `isAssignable`, `TypeTag::Array`, `Type::element` in
`include/apollo/pascal/Type.hpp`. See also the cross-link under Milestone 3
[Migration / follow-on notes](03-semantic-analysis.md#migration--follow-on-notes).
