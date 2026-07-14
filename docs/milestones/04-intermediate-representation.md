← [Project milestones index](../milestones.md)

## Milestone 4 — Intermediate representation

This document defines the **Milestone 4** shared intermediate representation (IR) for
Apollo: a language-neutral IR model, a Pascal lowerer that consumes the Milestone 3 typed
AST / symbol table, and a dumpable textual form suitable for unit tests and for Milestone 5
Gemini codegen.

It complements:

- [Project milestones](../milestones.md) (Milestone 4 scope)
- [Overview](../overview.md) (pipeline and architectural principles)
- [Milestone 3 — Semantic analysis](03-semantic-analysis.md) (typed AST to consume)
- [Milestone 5 — Code generator](05-code-generator.md) (next consumer)

### Goals

- Define a **shared**, language-neutral IR owned by Apollo (not Gemini’s BASIC IR, and not
  Pascal-specific opcodes).
- Lower **semantically clean** Pascal programs (M2/M3 v1 subset) into that IR — including
  `examples/hello.pas` and `examples/count.pas`.
- Represent **control flow** explicitly (basic blocks / structured lowers that yield a CFG).
- Reuse M3 `Type` notions or map them to IR types **deliberately** — no second ad-hoc type
  system without mapping rules.
- Expose IR from the driver (`apolloc --ir` / similar) for inspection; keep `--check` as the
  analyse-only path.
- Keep Gemini `.tbc` emission, runtime linking, and multi-language front-ends out of this
  milestone (Milestones 5–7).
- Leave `main` green after each stage (`cmake` build + `ctest`).

### Language / IR scope (v1)

Aligned with the Pascal subset already parsed and checked in M2/M3:

**In scope**

- IR module with procedures/functions, locals/params, temporaries, and typed values.
- Lowering of literals, identifiers, unary/binary ops, groups, calls (including console
  I/O builtins as IR call / runtime ops).
- Lowering of assignment, compound statements, `if` / `while` / `repeat` / `for`.
- Nested procedures/functions present in the AST (lower as nested or as flat IR functions
  with explicit nesting/frame convention documented when implemented).
- Stable **text dump** of IR for tests (human-readable, line-oriented or indented).

**Explicitly deferred**

- Gemini `.tbc` / instruction emission (Milestone 5).
- Host runtime / PickVM linking (Milestones 5–6).
- Full optimization pipelines (constant folding beyond what lowering naturally does,
  SSA construction if not required for v1, register allocation).
- Records, sets, pointers, `file` I/O, `case` / `with` / `goto` (unless forced by a fixture;
  default remains out).
- Second language front-ends targeting the IR (Milestone 7).

### Milestone slices (summary)

| Slice | Focus |
|-------|--------|
| **M4a** | Shared IR data model + textual dump + CMake wiring |
| **M4b** | Pascal straight-line lowering (exprs, assign, calls / writeln) |
| **M4c** | Control-flow lowering (`if` / loops / `for`) + subprogram frames |
| **M4d** | Driver `--ir`, debt close-out (array assignability), M4 finish / `0.4.0` |

Detailed staged delivery is below. Slices map 1:1 to Stages 1–4.

**Method note:** Mak introduces an intermediate “icode” layer before target code. Apollo
mirrors that with original code: first a real IR value, then lower easy statements, then
control flow, then expose dump and freeze the contract for codegen.

---

## Staged delivery plan

Work lands in four mergeable stages. Each stage should leave `main` green and update the
**Implementation status** section when closed.

### Stage 1 — Shared IR model & dump (M4a)

**Objective:** Land a usable IR representation and printer with **no** Pascal lowering yet
(or only a tiny hand-built fixture in tests). Establish headers, library target, and dump
format.

**Deliverables**

- Public IR headers under `include/apollo/ir/` (or `include/apollo/pascal/ir/` only if a
  shared tree is deferred — **prefer shared `apollo/ir`** so later languages reuse it).
- Implementation under `src/ir/` (new `apollo-ir` target) and/or complete the existing
  `src/pascal/ir/` placeholder once the shared/pascal split is fixed in Stage 1 notes.
- Core shapes (names flexible; intent fixed):
  - `Module` — list of functions / global metadata
  - `Function` — params, locals, entry block, ordered blocks
  - `BasicBlock` — label + instruction list + terminators
  - `Instr` / opcodes for arith, compare, load/store or copy, call, branches, return,
    and placeholders for I/O runtime ops
  - Value / temporary identity (IDs or operands)
- Text dump helper (`writeIrDump`) comparable in spirit to `writeAstDump`.
- Unit tests that construct a tiny IR graph in C++ and assert dump substrings.
- CMake: link `apollo-ir` as needed; do not break `apolloc` yet (no new CLI required in
  Stage 1).

**Out of scope for Stage 1**

- Walking the Pascal AST.
- CFG construction from `if` / loops.
- `--ir` CLI (Stage 4).

**Acceptance criteria**

- [ ] IR library builds and a dump-roundtrip-style unit test passes.
- [ ] Documented opcode / operand conventions live in this file (Stage 1 notes).
- [ ] Existing M1–M3 tests remain green.

**Status:** not started.

### Stage 2 — Straight-line Pascal lowering (M4b)

**Objective:** Lower expressions, assignments, and call/`writeln`-style statements in
straight-line compounds (no branching yet beyond what a call implies).

**Deliverables**

- `lowerToIr(ast::Program &, SymbolTable &, DiagnosticEngine &) → Module` (or equivalent)
  under Pascal IR code (`src/pascal/ir/`), consuming M3 annotations (`Expr::type`, symbols).
- Lower:
  - integer / real / boolean / char / string literals
  - loads of vars/params/consts
  - unary / binary ops matching M3 result types
  - assignment to vars / params / function result
  - procedure/function calls and builtins `write` / `writeln` / `read` / `readln` as IR
    calls or dedicated runtime ops (pick one and document)
- Programs that are only compounds of these forms (e.g. a synthetic “assign and writeln”
  fixture, and eventually shapes needed for `hello.pas`) produce a non-empty module.
- Unit tests: dump contains expected ops / names for a small Pascal fixture; invalid input
  is not re-checked here — assume `--check` clean (lowering may assert or skip on Error
  types).

**Out of scope for Stage 2**

- `if` / `while` / `repeat` / `for` CFG edges.
- Nested subprogram frame layout beyond a single function + program body as one IR function
  (expand in Stage 3 if needed).

**Acceptance criteria**

- [ ] A clean straight-line Pascal fixture lowers with zero diagnostics and a stable dump.
- [ ] Builtin `writeln` with a string (hello-shaped) appears in IR in the chosen form.
- [ ] M3 analyse tests remain green.

**Status:** not started.

### Stage 3 — Control flow & subprograms (M4c)

**Objective:** Lower structured control flow and multi-subprogram programs so
`examples/count.pas` (and similar) become well-formed IR CFGs.

**Deliverables**

- Lower `if` / `while` / `repeat` / `for` into basic blocks + conditional / unconditional
  branches (or an equivalent structured encoding that still exposes blocks for M5).
- Lower program block body and nested procedures/functions into IR functions; document
  nesting / activation strategy (flat functions with explicit names is acceptable for v1).
- `examples/count.pas` lowers cleanly (for + `writeln(i)`).
- Unit / golden dump tests for at least one branching and one looping fixture.
- Close or implement the **recursive array assignability** debt if Stage 3 fixtures need
  array values; otherwise leave explicitly until Stage 4 if still unused.

**Out of scope for Stage 3**

- `.tbc` emission.
- Optimizing jumps / empty-block cleanup beyond what’s needed for readable dumps.

**Acceptance criteria**

- [ ] `examples/hello.pas` and `examples/count.pas` lower with zero diagnostics.
- [ ] Loop / branch fixtures show distinct blocks (or documented structured form) in the dump.
- [ ] Existing tests remain green.

**Status:** not started.

### Stage 4 — Driver, debt close-out & Milestone 4 finish (M4d)

**Objective:** Expose IR from `apolloc`, finish M3→M4 debts needed for a stable lowerer, and
close Milestone 4.

**Deliverables**

- `apolloc --ir <file>` (prefer `-i` or `--ir`; avoid clashing with `-c` / `--check`):
  load → scan → parse → analyse → lower → write IR dump to stdout; diagnostics on stderr;
  non-zero exit on errors (same policy as `--ast` / `--check`).
- README documents `--ir`.
- Resolve **recursive array assignability** in `isAssignable` (see Open debts below) before
  claiming array-capable IR, or document “arrays not lowered yet” if still out of v1 IR —
  **default for close-out:** implement the assignability fix and keep array *lowering*
  minimal unless fixtures demand it.
- Update this document’s **Implementation status** and definition of done.
- Version / tag policy: bump to **`0.4.0`** when closing (tag `v0.4.0` as a separate
  release follow-up).

**Acceptance criteria**

- [ ] `--ir` on `examples/hello.pas` and `examples/count.pas` exits 0 and prints IR.
- [ ] `--ir` on a semantic-error fixture exits non-zero (no IR dump required).
- [ ] README documents `--list`, `--tokens`, `--ast`, `--check`, and `--ir`.
- [ ] All Stage 1–4 tests pass under `ctest`.

**Status:** not started.

### Suggested staging cadence

1. **Stage 1** — IR types + dump.
2. **Stage 2** — straight-line Pascal lowering.
3. **Stage 3** — control flow + subprograms; hello/count IR-ready.
4. **Stage 4** — `--ir`, assignability debt, close-out / `0.4.0`.

Do not start Milestone 5 `.tbc` work on `main` until Stage 3’s IR shapes for the v1
examples are stable enough to lower (Stage 4 may still be in flight if the dump format is
frozen).

---

## Design targets (not frozen APIs)

### Shared vs Pascal-local IR

| Choice | Default for M4 |
|--------|----------------|
| Ownership | Shared `apollo::ir` used by Pascal lowerer |
| Pascal AST | Remains Pascal-specific; lowerer is the only AST→IR bridge |
| Gemini coupling | None in M4 — IR must not include `.tbc` opcodes verbatim |

### Value model

Prefer **explicit temporaries** and simple copy/load/store-style ops over hiding everything
in expression trees. SSA is **not** required for v1; introduce φ-nodes only if a later stage
needs them.

### Types in IR

Map M3 canonical tags to IR types (`i32`/`f64`/`bool`/`char` / string ref / array ref — exact
names TBD in Stage 1 notes). Keep a table in this document once Stage 1 lands.

### Console I/O

Lower `write` / `writeln` / `read` / `readln` as runtime calls with a fixed Apollo runtime
name mangling (e.g. `@writeln`) so Milestone 5 can bind them to Gemini host ops without
revisiting Pascal AST.

---

## Open debts / prerequisites (from M3)

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

**Default schedule:** implement in **Stage 4** (or Stage 3 if an IR fixture needs arrays
sooner).

---

## Scope and non-goals

### In scope (M4)

- Shared IR model, dump, and Pascal lowerer for the checked v1 subset.
- CFG (or block-list) suitable for a straightforward Milestone 5 walk.
- `apolloc --ir` at close-out.
- Array assignability correctness debt from M3.

### Deferred (not in this milestone)

- `.tbc` / Gemini codegen (Milestone 5).
- Standalone VM execution (Milestone 6).
- Additional source languages (Milestone 7).
- Heavy optimization / register allocation.

---

## Component layout

| Area | Location |
|------|----------|
| Shared IR headers | `include/apollo/ir/` (preferred) |
| Shared IR impl | `src/ir/` → `apollo-ir` |
| Pascal lowerer | `src/pascal/ir/` (consumes ast + semantic) |
| Existing placeholders | `src/pascal/ir/.gitkeep` — replace with real sources |
| Driver | `src/tools/apolloc.cpp` (`--ir` in Stage 4) |
| Tests | `tests/` (IR unit + CLI) |
| Examples | `examples/hello.pas`, `examples/count.pas` (must `--ir` clean at close-out) |

---

## Test matrix

| Stage | Minimum coverage |
|-------|------------------|
| 1 | Hand-built IR dumps; library links |
| 2 | Straight-line Pascal → IR; writeln string |
| 3 | Branch + loop lowers; hello + count IR |
| 4 | `--ir` exit 0 / non-zero; README; assignability if arrays matter |

---

## Boundary with adjacent milestones

| Concern | Milestone 3 | Milestone 4 | Milestone 5 |
|---------|-------------|-------------|-------------|
| Types / checking | Owns | Consumes / maps | Uses IR types |
| AST | Owns | Lowers | Does not see AST |
| Shared IR | No | Owns | Consumes |
| `.tbc` / Gemini | No | No | Yes |
| `--check` | Yes | Still valid | Still valid |
| `--ir` | No | Yes (Stage 4) | May still dump IR |

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Shared IR model & dump | not started |
| Stage 2 — Straight-line Pascal lowering | not started |
| Stage 3 — Control flow & subprograms | not started |
| Stage 4 — `--ir`, close-out | not started |

---

## Definition of done (Milestone 4)

- [ ] Stages 1–4 acceptance criteria checked off.
- [ ] `ctest` green on a clean configure/build.
- [ ] README documents `apolloc --ir`.
- [ ] `examples/hello.pas` and `examples/count.pas` pass `--ir`.
- [ ] This status table marked completed.
- [ ] Version policy recorded (default: report `0.4.0`; cut git tag `v0.4.0` as follow-up).
- [ ] Recursive array assignability fixed or explicitly waived with rationale in Stage 4 notes.

---

## Migration / follow-on notes

- Milestone 5 should walk IR only — avoid reaching back into Pascal AST.
- Prefer stable dump formats early; treat dump churn after Stage 3 as a soft API break for
  golden tests.
- Keep Gemini opcode details out of `apollo::ir` until M5 binding layer.
- When IR docs outgrow this page, split to `docs/ir.md` and link it from here.
