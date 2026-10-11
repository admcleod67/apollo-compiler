← [Project milestones index](../milestones.md)

## Milestone 10 — Pascal dialect polish (optional)

This document defines **Milestone 10**: optional Pascal fidelity and convenience after
the Gemini Pick product bar ([Milestone 8](08-pascal-runtime-library.md) +
[Milestone 9](09-pick-database-api.md)), and **before** multi-language expansion
([Milestone 11](11-multi-language-expansion.md)).

**Status: planned / optional.** Skip or thin this milestone if PickDB + Wirth files are
enough for the near-term product; resume when console formatting or string ergonomics
matter more than a second language.

### Goals (summary)

Close the highest-value dialect gaps that remain after M8/M9 without chasing full ISO /
Turbo Pascal 3.

### Priority order (within this milestone)

| Priority | Feature | Notes |
|----------|---------|-------|
| **P0** | `write` / `writeln` field widths and richer real formatting (e.g. `x:8:2`) | Main remaining console fidelity gap; likely module or formatting ABI |
| **P1** | Turbo-ish string helpers (`length`, `copy`, `pos`, …) | Useful with Pick string records; pin exact set in Stage notes |
| **P2** | Nested record field types; enumerations / subranges (as needed) | Language polish for structured data |
| **Backlog** | Units / `uses`; sets; pointers / `new` / `dispose`; `with`; `goto` / `label`; `tan`; console-only `eof` / `eoln` | Separate follow-ons unless explicitly pulled in |

---

## In scope / out of scope

**In scope (M10)**

- P0 field widths / real print defaults (document remaining deviations if any).
- Optionally P1 string helpers and a thin P2 type-system slice if cheap after P0.
- Dialect + examples + `ctest`.

**Explicitly out**

- PickDB (Milestone 9).
- Wirth file façade work (Milestone 8 Stage 3).
- Full units / sets / pointers / objects / ISO claims unless a stage note expands scope.
- Second languages (Milestone 11).

---

## Suggested staged delivery

### Stage 1 — Field widths / real formatting

**Acceptance criteria**

- [ ] Documented `write` width forms analyse and emit via the chosen runtime path.
- [ ] Dialect lists supported formatting; examples cover common cases.
- [ ] `ctest` green.

### Stage 2 — String helpers (optional)

**Acceptance criteria**

- [ ] Locked helper set analyses, lowers, emits; wrong arity/types diagnose.
- [ ] Dialect updated; `ctest` green.

### Stage 3 — Nested records / ordinal types (optional)

Pin only if still needed after Stage 1–2; otherwise leave as backlog.

---

## Success criteria (draft)

- [ ] P0 shipped or explicitly deferred with rationale.
- [ ] Multi-language expansion still deferred as Milestone 11.
- [ ] Dialect overview remains the user-facing source of truth.
- [ ] `ctest` green on a clean configure/build.

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Field widths / real formatting | not started |
| Stage 2 — String helpers | not started (optional) |
| Stage 3 — Nested records / ordinal types | not started (optional) |

---

## Boundary with adjacent milestones

| Concern | M8–M9 | M10 | M11 |
|---------|-------|-----|-----|
| Pick / Wirth files | Owns | Regression | — |
| Formatting / strings / light types | Deferred | Owns | — |
| Second languages | Deferred | Deferred | Owns when resumed |
