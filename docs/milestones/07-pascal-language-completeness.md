← [Project milestones index](../milestones.md)

## Milestone 7 — Pascal language completeness (Wirth console / Pascal80-style)

This document defines **Milestone 7**: deepen the Pascal front-end toward a **Wirth /
Jensen–Wirth console dialect**, aiming for a language level comparable to HiSoft
Pascal80 or early Turbo Pascal **without** chasing full Turbo Pascal 3 or ISO 7185
fidelity.

It follows:

- [Milestone 5 — Code generator](05-code-generator.md) (emit path exists)
- [Milestone 6 — Standalone VM execution](06-standalone-vm-execution.md) (completed;
  hello/count on `gemini-vm`)
- And **defers** [Milestone 8 — Multi-language expansion](08-multi-language-expansion.md)
  until Pascal is a credible console language

**Priority:** language completeness over additional source languages.

### Goals (summary)

- Make Apollo Pascal feel like a real **console Wirth Pascal** for teaching and small
  programs runnable on Gemini’s standalone VM.
- Close end-to-end gaps (parse → analyse → lower → emit) for the chosen subset.
- Add a small set of **Turbo-lite extensions** only when they unlock real workflows
  (notably `{$I}` source inclusion).
- Keep IR and codegen shared and language-neutral; do not reintroduce Pick-specific I/O.

### Target dialect

| Layer | Intent |
|-------|--------|
| **Core** | Wirth / Jensen–Wirth Pascal, console-oriented |
| **Compatibility aspirational** | HiSoft Pascal80 / Turbo Pascal 3 *console* programs |
| **Not a promise** | Full TP3, ISO 7185, units/objects/overlays/graphics |

Treat Pascal80 / TP3 as **fixture sources of inspiration**, not as a byte-compatible
dialect claim.

---

## Baseline vs gaps (today)

Apollo already has a strong **procedural skeleton**:

- `program`, `const` / `type` / `var`, top-level `procedure` / `function`
- `integer` / `boolean` / `char` / string literals; type aliases; array *denoters*
- Compound, assign, call, `if` / `while` / `repeat` / `for`
- Console builtins `write` / `writeln` / `read` / `readln`
- End-to-end emit for hello/count-style programs

Major Wirth gaps (not yet end-to-end):

| Area | Status |
|------|--------|
| Array **indexing** `a[i]` | Denoters / whole-array assign only |
| `real` / `mod` through emit | Analysed / IR partial; codegen diagnoses `ConstF64`, `Mod` |
| `record` / field select | Keywords reserved; not in grammar |
| `case` | Keyword reserved; not in grammar |
| Nested procs with up-level locals | Parsed; lowering diagnoses |
| Sets, pointers, `file`, `goto`/`with` | Out / diagnose / keywords only |
| Compiler directives / `{$I}` | `{...}` is comment-only today |

---

## In scope / out of scope

**In scope (M7)**

- Array indexing through emit (Gemini `DIM_ARRAY` / `LOAD_ARR` / `STORE_ARR` or equivalent).
- Scalar completeness for console programs: `real` emit convention; `mod` emit.
- `record` types and field load/store (flat records first).
- `case` statements on ordinal selectors.
- `{$I filename}` include directives (scanner / source stack).
- Nested subprogram policy: either implement one-level up-level access, or keep a clear
  diagnostic with a documented subset (prefer implementing shallow nesting if cheap).
- Examples and `ctest` coverage that run on `gemini-vm` where practical.
- Docs / README updated to describe the supported dialect.

**Explicitly deferred (beyond M7 or later slices)**

- Units (`unit` / `uses` / interface–implementation).
- Full directive language (`{$IFDEF}`, `{$R+}`, …) beyond `{$I}` (and maybe ignore-unknown
  with a warning).
- Sets, pointers / heap, `file` I/O, `with`, `goto`/`label` (unless a later stage needs
  one of them).
- Enumerated and subrange *types* as a full type system (array bounds may stay as today
  until needed).
- Objects, overlays, inline assembler, graphics, TP string length types.
- Additional source languages ([Milestone 8](08-multi-language-expansion.md)).

---

## Suggested staged delivery

Work lands in mergeable stages. Each stage should leave `main` green and update the
**Implementation status** section when closed.

### Stage 1 — Runnable scalar / array debt (M7a)

**Objective:** Close emit holes that already exist in earlier milestones.

- Emit `real` (`ConstF64` / Gemini numeric convention as documented).
- Emit `mod`.
- Array indexing expressions and element assignment through IR + `.tbc`.
- Tests: fixtures that `--emit` and optionally run under `gemini-vm`.

### Stage 2 — Records (M7b)

**Objective:** Classic Wirth structured data.

- Parse / analyse `record` … `end` and field selection.
- Lower and emit field load/store (document layout / mangling).
- Whole-record assign if straightforward; otherwise elementwise / diagnose clearly.

### Stage 3 — `case` + nesting polish (M7c)

**Objective:** Control-flow and subprogram completeness for console programs.

- `case` … `of` … `end` on integer/char (and boolean if cheap).
- Nested subprograms: shallow up-level locals or a frozen “top-level only” diagnostic
  policy documented in Stage notes.

### Stage 4 — `{$I}` and dialect close-out (M7d)

**Objective:** Multi-file sources without units; freeze the supported dialect.

- Directive-aware `{`$…`}` in the scanner; implement `{$I}` / `{$i}` include stack
  (cycle detection, depth limit, path relative to includer).
- Unknown directives: diagnose or warn (pick one; document it).
- Dialect summary in README / this file; examples using includes.
- Version bump policy when closing: report **`0.7.0`** (`PROJECT_VERSION`); cut git tag
  `v0.7.0` as a separate release follow-up.

---

## Success criteria (draft)

- [ ] Array indexing programs emit and run on `gemini-vm`.
- [ ] `real` and `mod` no longer hard-fail in codegen for the supported subset.
- [ ] Flat `record` field access emits and runs.
- [ ] `case` on ordinal types emits and runs.
- [ ] `{$I}` includes compose a multi-file program that `--emit`s cleanly.
- [ ] README documents the supported Wirth console / Pascal80-style dialect and explicit
  non-goals (units, files, pointers, sets, …).
- [ ] Milestone 8 remains deferred; no second language front-end started as part of M7.
- [ ] `ctest` green on a clean configure/build.

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Scalar / array debt | not started |
| Stage 2 — Records | not started |
| Stage 3 — `case` + nesting | not started |
| Stage 4 — `{$I}` + dialect close-out | not started |

---

## Boundary with adjacent milestones

| Concern | M5–M6 | M7 | M8 |
|---------|-------|----|----|
| Runnable hello/count | Done | Regression | — |
| Wirth data / control gaps | Partial | Owns | — |
| `{$I}` includes | No | Yes | — |
| Units / full TP3 | No | No | No (still later) |
| Second languages | No | No | Deferred |

---

## Follow-on (beyond M7)

- Units and separate compilation.
- Sets, pointers, file I/O.
- Richer directives and range-check pragmas.
- Switch console builtins from `PRINT_*` to `CALL_FUNC` + Pascal module when Gemini’s
  module path is ready.
- [Milestone 8 — Multi-language expansion](08-multi-language-expansion.md) once Pascal is
  the reference front-end.
