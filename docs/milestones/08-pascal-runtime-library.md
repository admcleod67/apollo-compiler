← [Project milestones index](../milestones.md)

## Milestone 8 — Pascal runtime library (standard functions and I/O)

This document defines **Milestone 8**: deepen Apollo Pascal beyond the Milestone 7
**language skeleton** by shipping a usable **runtime surface** — standard functions,
console I/O fidelity, and (last) file I/O — before any second source language.

It follows:

- [Milestone 7 — Pascal language completeness](07-pascal-language-completeness.md)
  (completed dialect: records, arrays, `case`, `{$I}`, …)
- [Pascal dialect](../pascal-dialect.md) (supported behaviour today)
- And **defers** [Milestone 9 — Multi-language expansion](09-multi-language-expansion.md)
  until this runtime track is far enough for teaching and small console programs

**Priority:** finish Pascal as a credible console language (library + I/O) before BASIC /
COMAL / Fortran / COBOL front-ends.

### Goals (summary)

- Grow beyond console-only builtins (`write` / `writeln` / `read` / `readln`).
- Prefer **compiler-lowered** ordinal and arithmetic helpers first; use a language module /
  `CALL_FUNC` path where transcendental math or richer I/O needs the VM ABI.
- Improve console I/O fidelity without blocking on files.
- Add Pascal **file** support only after a host-agnostic filesystem façade exists in the
  VM/host stack.
- Keep IR language-neutral; bind Pascal-specific names in analyse / lower / emit (and
  optional runtime module), not by baking Pick paths into the front-end.

### Baseline (end of M7)

| Area | Status |
|------|--------|
| Dialect skeleton | Complete enough for console programs (see dialect doc) |
| Builtins | Console I/O; Stage 1 ordinal/arithmetic functions |
| Console formatting | Stage 2 opcodes; no `write` field widths; real `PRINT_VAL` defaults may still differ |
| `file` / `text` | Out of scope; keyword reserved |
| Math / ordinal library | Stage 1 + Stage 1b shipped (`tan` not provided) |

---

## In scope / out of scope

**In scope (M8)**

- Standard **functions** and related predeclared helpers (staged; see below).
- Console I/O **completeness** (formatting, input of `real`, optional module binding).
- Pascal **file** types and sequential I/O once the host FS façade is available.
- Updates to [`pascal-dialect.md`](../pascal-dialect.md), examples, and `ctest`.

**Explicitly deferred (beyond M8 or later)**

- Units / separate compilation.
- Sets, pointers / heap (`new` / `dispose`), `with`, `goto` / `label`.
- Packed arrays (`pack` / `unpack`), `page`, Turbo string helpers (`length`, `copy`,
  `pos`, …).
- Full directive language beyond `{$I}` + warn-on-unknown.
- Objects, overlays, graphics, ISO 7185 / full TP3 claims.
- Additional source languages ([Milestone 9](09-multi-language-expansion.md)).

---

## Suggested staged delivery

Each stage should leave `main` green and update this document’s **Implementation status**
when closed. Stages are ordered by dependency and teaching value: **functions first**,
**console next**, **files last**.

Milestone 8’s **teaching / console bar** is Stages **1 and 2**. Stages **1b** and **3**
stay in this milestone (do not spawn new numbered milestones) but may remain open if
their Gemini prerequisites are not ready; M8 can still close for planning purposes with
those gaps documented.

### Stage 1 — Standard functions (ordinal and arithmetic)

**Objective:** Classic Wirth-style helpers that many console programs expect, without a
filesystem or transcendental math module.

**Locked Stage 1 set** (implement this list; do not reopen it mid-stage):

| Function | Argument | Result | Notes |
|----------|----------|--------|-------|
| `ord(x)` | `integer`, `char`, or `boolean` | `integer` | `char` → code; `boolean` → `0`/`1` |
| `chr(n)` | `integer` | `char` | Pin accepted range and out-of-range diagnosis in Stage notes |
| `succ(x)` | `integer`, `char`, or `boolean` | same type as `x` | |
| `pred(x)` | `integer`, `char`, or `boolean` | same type as `x` | |
| `odd(i)` | `integer` | `boolean` | |
| `abs(x)` | `integer` or `real` | same type as `x` | |
| `sqr(x)` | `integer` or `real` | same type as `x` | |
| `trunc(x)` | `real` | `integer` | Toward zero |
| `round(x)` | `real` | `integer` | Pin half-value ties in Stage notes |

**Approach (pinned intent):**

- Declare as builtins (or equivalent predeclared functions) in the symbol table.
- Lower to IR / small emit sequences (no new VM opcodes required for this set).
- Document signatures and edge cases in the dialect overview.
- Wrong arity or types diagnose; these names are expression-valued (unlike console I/O).

**Explicit non-goals (Stage 1):** transcendentals (`sqrt`, `sin`, … — Stage 1b);
`eof` / `eoln` (Stage 3, optionally console with Stage 2); `new` / `dispose`;
`pack` / `unpack`; `page`; Turbo string helpers (`length`, `copy`, `pos`, …).

**Acceptance criteria**

- [x] Listed Stage 1 functions analyse, lower, and emit; wrong arity/types diagnose.
- [x] Examples and/or tests cover each function.
- [x] [`pascal-dialect.md`](../pascal-dialect.md) lists the supported set and gaps.
- [x] `ctest` green.

**Stage 1 notes (pinned)**

- Implemented as expression-valued builtins; console I/O remains statement-only.
- Lowered inline to IR (`Copy`, `Add`/`Sub`, `Mod`, `Mul`, `Abs`, `ConvertI32`, BranchIf
  diamonds for `round` / real `abs`); emit uses `ABS_INT` and `COERCE_INT` where needed.
- **`chr`:** constant arguments outside `0..255` diagnose; non-constant arguments are
  unchecked (no bitwise mask opcode).
- **`round`:** half away from zero — add `+0.5` or `-0.5` by sign, then `COERCE_INT`.
- **`succ`/`pred` on boolean:** constant `succ(true)` / `pred(false)` diagnose; variables
  are unchecked (same philosophy as array bounds).
- **`odd`:** `(i mod 2) <> 0` with Turbo truncated `mod`.

**Status:** completed.

### Stage 1b — Transcendental math (module-gated)

**Objective:** The remaining Jensen–Wirth arithmetic functions that need a real math
runtime rather than a few IR ops.

**Locked set:** `sqrt`, `sin`, `cos`, `arctan`, `ln`, `exp` — each `real` → `real`
(integer arguments widen). `tan` is not in the locked set (Gemini math id 3 unused).

**Prerequisite:** Gemini shared **`math`** module / `CALL_FUNC` namespace **6** (Milestone 21)
— met. Do **not** block Stage 1 on this slice.

**Acceptance criteria**

- [x] Listed functions analyse, lower, and emit via the module/ABI path.
- [x] Wrong arity/types diagnose; domain errors follow documented runtime behaviour.
- [x] Dialect doc lists the supported math set.
- [x] `ctest` green.

**Stage 1b notes**

| Pascal | Emit |
|--------|------|
| `sqrt` | `CALL_FUNC 6, 0, 1` |
| `sin` | `CALL_FUNC 6, 1, 1` |
| `cos` | `CALL_FUNC 6, 2, 1` |
| `arctan` | `CALL_FUNC 6, 4, 1` |
| `ln` | `CALL_FUNC 6, 5, 1` |
| `exp` | `CALL_FUNC 6, 6, 1` |

- Angles are **radians**. Domain errors use Gemini’s `MATH:` prefix (`SQRT` / `LN`).
- Programs that call these must load **`gemini-module-math`** (`gemini-vm --modules` or
  bootstrap module path).
- Lowered as value-producing `CallRuntime`; integer args widen via `COERCE_FLT`.

**Status:** completed.

### Stage 2 — Console I/O fidelity

**Objective:** Make existing console builtins behave more like Pascal programmers expect
by consuming Gemini M20 core opcodes in the emit binding table (no module migration).

**In this stage**

| Pascal behaviour | Emit |
|------------------|------|
| `write` / `writeln` of `char` / char-literal | `PRINT_CHAR` (glyph, not decimal code) |
| Other printable write args | `PRINT_VAL` (+ `PRINT_EOL` for writeln) |
| `read` / `readln` of `real` | `INPUT_FLT` |
| `Integer → Real` widen (`ConvertF64`) | `COERCE_FLT` |
| Integer `mod` | `MOD` (Turbo truncated semantics) |

**Explicit non-goals (Stage 2):** field widths (`write(x:8:2)`) and TP-style real
formatting (document as remaining deviations); `CALL_FUNC` / language-module migration
for console I/O; `eof` / `eoln` (Stage 3); softening `DIM_ARRAY` / `MAT_*`.

**Prerequisite:** Gemini M20 opcodes (`PRINT_CHAR`, `INPUT_FLT`, `COERCE_FLT`, `MOD`) —
met.

**Acceptance criteria**

- [x] Dialect doc updated for formatting and I/O behaviour.
- [x] Regression tests for console builtins remain green; new fidelity cases where
      behaviour changes.
- [x] Binding table remains the single switch point (write arg types carried on
      `CallRuntime`; no IR op redesign).

**Stage 2 notes**

- `CallRuntime` for write/writeln carries `argTypes` so emit can dispatch `PRINT_CHAR`
  vs `PRINT_VAL`.
- Remaining deviations: no Pascal field widths; real output still uses VM `PRINT_VAL`
  defaults (for example `1.0` may print as `1`).
- Bootstrap opcodes retained (no `CALL_FUNC` console path yet).

**Status:** completed.

### Stage 3 — File I/O

**Objective:** Wirth-style `file` / `text` sequential I/O on a **host-agnostic** FS façade.

**Prerequisite:** Gemini / host filesystem library usable from the standalone runner and
Pick-backed hosts without hard-wiring POSIX or VOC paths in Apollo (see Milestone 7 —
*Why Pascal `file` I/O waits*).

- Types: `file of T`, `text` (scope pinned in Stage notes).
- Operations: a minimal sequential set (for example `reset` / `rewrite`, typed
  `read` / `write`, `eof` / `eoln` as appropriate).
- Builtin or module binding for file ops; diagnose unsupported forms clearly.

**Acceptance criteria**

- [ ] Host FS façade available and documented on the VM side.
- [ ] A small multi-file or file-I/O example `--emit`s and runs on the standalone runner.
- [ ] Dialect doc describes supported file surface and non-goals.
- [ ] `ctest` green.

**Status:** not started (blocked on host FS).

---

## Success criteria (draft)

- [x] Stage 1 standard functions usable in examples and tests.
- [x] Console I/O behaviour documented; major fidelity gaps closed or explicitly accepted
      (field widths / real print defaults remain documented deviations).
- [x] Stage 1b either shipped or still clearly blocked on the language module / `CALL_FUNC`
      path, with the Wirth math set named here.
- [ ] File I/O either shipped (Stage 3) or still clearly blocked with documented rationale.
      **M8 may close after Stages 1–2** if the host FS façade has not arrived; Stage 3
      remains the home for files rather than a new milestone.
- [x] Multi-language expansion remains deferred as Milestone 9.
- [x] Dialect overview stays the user-facing source of truth for what is supported.
- [x] `ctest` green on a clean configure/build.

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Standard functions | completed |
| Stage 1b — Transcendental math | completed |
| Stage 2 — Console I/O fidelity | completed |
| Stage 3 — File I/O | not started (blocked on host FS) |

---

## Boundary with adjacent milestones

| Concern | M7 | M8 | M9 |
|---------|----|----|-----|
| Dialect skeleton | Done | Regression | — |
| Standard functions / console / files | Deferred | Owns | — |
| Second languages | Deferred | Deferred | Owns when resumed |

---

## Out of scope reminders

- Do not start Milestone 9 front-ends during M8.
- Do not implement Pascal files against a Pick-only or POSIX-only path.
- Units, sets, and pointers remain separate follow-ons unless explicitly pulled into a
  stage.
- Do not expand Stage 1 beyond the locked ordinal/arithmetic set.
