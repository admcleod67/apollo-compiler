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
- `integer` / `boolean` / `char` / `real` / string literals; type aliases; arrays
- Flat `record` types and field select; whole-record and whole-array assign
- Compound, assign, call, `if` / `while` / `repeat` / `for` / `case`
- Console builtins `write` / `writeln` / `read` / `readln`
- `{$I}` / `{$i}` include directives (unknown `{$…}` → warning)
- End-to-end emit for hello/count-style programs and Stage fixtures

Wirth surface still deferred or diagnosed:

| Area | Status |
|------|--------|
| Array **indexing** `a[i]` | Implemented (const bounds; value array params via call-site copy) |
| `real` / `mod` through emit | Implemented (`PUSH_FLT`, mod sequence, `Integer` widening) |
| `record` / field select | Implemented (flat only; nested field types diagnosed) |
| `case` | Implemented (ordinal selector; linear compare / `BranchIf` chain) |
| Nested procs with up-level locals | Diagnosed (top-level subprograms only) |
| Sets, pointers, `file`, `goto`/`with` | Out / diagnose / keywords only |
| Compiler directives beyond `{$I}` | Unknown `{$…}` warn; richer directives deferred |

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

### Why Pascal `file` I/O waits

Wirth-style files (`file of T`, `text`, sequential `reset`/`rewrite` / `get`/`put`) are a
**language** contract. Binding those ops to durable storage is a **host** contract:

| Layer | Concern |
|-------|---------|
| Apollo | Types, lowering, stable IR / runtime names (or later `CALL_FUNC` IDs) |
| Gemini host façade | Open/read/write (or get/put) against **either** a portable host FS or Pick VOC/MD |

Standalone `gemini-vm` currently leaves the filesystem unbound; Pick Application/Service
editions bind a Pick-shaped backend. Implementing Pascal files before a **shared
filesystem façade** would hard-wire either POSIX paths (wrong inside Gemini) or Pick
paths (wrong for standalone).

Milestone 7 therefore adds **records** as in-memory Wirth structure (and a prerequisite
for a later `file of record`) but does **not** ship file I/O. Resume files only after
Gemini exposes a host-agnostic FS surface that both `gemini-vm` and Pick hosts can bind —
see also [Milestone 6 follow-on](06-standalone-vm-execution.md#follow-on-beyond-m6).

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

**Acceptance criteria**

- [x] `real` literals/vars emit `PUSH_FLT` (no codegen hard-fail).
- [x] `real` arithmetic is faithful: literal precision round-trips and `Integer → Real`
  widens before real division (follow-up **1b**).
- [x] Integer `mod` emits without a language module.
- [x] Indexed array load/store emit `DIM_ARRAY` / `LOAD_ARR` / `STORE_ARR`.
- [x] Every accepted array bound form lowers correctly (follow-up **1a**).
- [x] Stage fixtures pass under `ctest`.

**Stage 1 notes**

- **`real`:** `Op::ConstF64` → Gemini `PUSH_FLT`. Binary/`Neg` reuse `ADD`/`SUB`/… (VM
  `Value` is `int|double|string`), with `Integer` operands widened first — see the pinned
  semantics below.
- **`mod`:** Gemini has no core `MOD` opcode. Emit `a - (a div b) * b` on the stack as
  `LOAD a; LOAD a; LOAD b; DIV; LOAD b; MUL; SUB` (toward-zero `DIV`). Do not call
  BASIC `CALL_FUNC` MOD.
- **Arrays:** const `[lo..hi]` bounds stored on `Type`; IR `DimArray` / `LoadIndex` /
  `StoreIndex`; emit remaps Pascal index to VM 1-based via `SUB (lo - 1)` (omitted when
  `lo` is 1), then `DIM_ARRAY` + `MAT_INIT` (string arrays skip the fill) +
  `LOAD_ARR`/`STORE_ARR`. Whole-array assignment still diagnosed.
- **Smoke:** `apolloc --emit <file> | ../pick-system/build/src/gemini-vm /dev/stdin`
  (no CMake dependency on pick-system).

**Semantics pinned in Stage 1**

- `mod` uses truncated division, so `-7 mod 2` is `-1` and `7 mod -2` is `1`. That matches
  Turbo Pascal, **not** ISO / Wirth (which yields `1` and treats a non-positive divisor as
  an error). Turbo compatibility is the intended choice for this dialect.
- Array bounds checking comes from the VM, which reports mangled slot names
  (`Array index out of bounds: MAIN$A`). Apollo emits no range check of its own.
- Bounds and element counts must fit the VM's 32-bit `int`; analyse diagnoses anything
  wider rather than letting an oversized `PUSH_INT` fail at `.tbc` load time (**1a**).
- Gemini picks integer or float arithmetic from the *runtime* operand types, so lowering
  widens `Integer → Real` explicitly (IR `ConvertF64`). Integer literals in a real context
  fold to `PUSH_FLT`; other values multiply by `1.0`, because the VM has no int-to-float
  opcode (only `CoerceInt`) and any `double` operand forces a `double` result. A future
  `COERCE_FLT` would replace the pair (**1b**).
- Integer and real arrays are zero-filled with `PUSH_INT 0` / `PUSH_FLT 0.0` + `MAT_INIT`.
  String arrays emit bare `DIM_ARRAY`, which already fills with `""` (**1c**).
- Index remapping folds to a single `SUB (lo - 1)` and is omitted when `lo` is 1. The
  two-step `SUB lo; ADD 1` form is kept only when `lo - 1` is below `INT32_MIN` (so
  `array [INT32_MIN..…]`), because the VM parses `PUSH_INT` with `std::stoi` (**1c**).
- `real` literals outside `double` range are diagnosed rather than emitted as `0` (**1b**).
- **Known deviation:** real *output* uses the VM's `PRINT_VAL` formatting, so `1.0` prints
  as `1` rather than Pascal's scientific default, and `write(x:8:2)` field widths are not
  supported. Both belong to the Stage 4 dialect close-out.
- `readln` of a `real` reads a line with `INPUT_STR` and parses it via the same widening
  multiply, since Gemini has no float input opcode (**1b**).

**Stage 1 review findings**

A post-implementation review (diff read plus `apolloc --emit` runs piped through
`gemini-vm`) confirmed the happy path — `array [1..5]` filled and summed in a `for` loop
prints `55`, `17 mod 5` prints `2` — and found four defects, three of which miscompile
silently instead of diagnosing:

| # | Defect | Symptom |
|---|--------|---------|
| D1 | `Lower.cpp` re-derives array bounds with a weaker evaluator than `Analyse.cpp` (plain integer literal, or a const whose initializer is one) and silently defaults to `1` | `array [-3..3]`, `array [1..(4)]`, and a const bound declared inside a subprogram get the wrong `DIM_ARRAY` size and index remap; `a[-3] := 7` traps as `Array index out of bounds: MAIN$A` |
| D2 | `TbcWriter::pushFlt` writes through the default stream precision (6 significant digits) | `3.14159265358979` prints `3.14159`; `123456789.5` prints `123457000` |
| D3 | No `Integer → Real` widening: `/` and `div` both emit a bare `DIV`, and the VM picks integer vs float division from runtime operand types | `x: real; x := 1; writeln(x / 2)` prints `0`, and so does `writeln(1 / 2)` |
| D4 | Array-typed parameters pass analyse and emit `LOAD_VAR <array slot>`, but VM arrays live in a separate map from scalars | `p(arr)` fails at run time with `Undefined variable: MAIN$ARR`; `Param::arrayLow` / `arrayHigh` / `arrayElement` is dead metadata |

Contributing design weakness: `makeArray` sets `hasBounds` unconditionally, so the flag
cannot distinguish *resolved* bounds from *guessed* ones — which is what let D1 pass
silently.

Follow-ups land as three separate plans, each independently green and mergeable. D1 is
fixed and D4 is diagnosed as of **1a**; D2 and D3 are fixed in **1b**; **1c** is the
codegen-quality pass (no semantic change).

**1a — Array bound plumbing (D1, D4) — completed**

- `analyse()` records each resolved type on `ast::TypeDenoter::resolved` (mirroring
  `Expr::type`), and lowering reads that annotation. `Lower`'s duplicate denoter resolver
  is gone, so a bound can no longer be guessed; a missing annotation is reported as an
  internal error instead of defaulting to `1..1`.
- Removing that resolver also fixes a second silent hazard it caused: a subprogram-local
  `type` alias used to lower to `IrType::Error`, which made `readln` on such a local fail
  in codegen.
- Because analyse is now the only producer of array `Type`s, `hasBounds` is honest by
  construction and needs no `std::optional` refactor.
- Bounds are validated against the VM's 32-bit `int` range, and array-typed parameters and
  function results are diagnosed until Stage 2 implements them.
- Tests: negative low bound, parenthesised bound, program-level and subprogram-local const
  bounds, subprogram-local type alias, plus the three new diagnostics.

**1b — Real numerics (D2, D3) — completed**

- `PUSH_FLT` operands use the shortest spelling that reads back as the same `double`, and
  integral values keep a decimal point.
- IR gained `Op::ConvertF64`. Lowering widens wherever an Integer value reaches a Real
  context: real-typed operators (so `1 / 2` is `0.5`), assignment to a `real` variable,
  `array of real` element, function result, and `real` parameters. Integer literals fold
  straight to `ConstF64`, so the widening multiply only appears for integer *values*.
- Out-of-range `real` literals are diagnosed, and `readln` of a `real` no longer hard-fails
  codegen.
- Tests: precision round-trip in the codegen test; emit fixtures for real division,
  widening, real array elements, real parameters, and real `readln`; analyse fixtures for
  literal range.

**1c — Codegen quality and golden `.tbc` (no semantic change) — completed**

- Array zero-init is `PUSH_INT 0` / `PUSH_FLT 0.0` + `MAT_INIT`, dropping the `$dim` /
  `$i` helper variables and two labels per array. Emitted control flow matches the IR
  CFG again. String arrays skip the fill: `DIM_ARRAY` already writes `""`.
- Index remapping is a single `SUB (lo - 1)`, omitted when `lo` is 1, with the two-step
  form retained only for `lo == INT32_MIN`.
- `mod` is the seven-op stack sequence rather than `$quot` / `$prod` spills.
- Void IR instructions (`StoreLocal`, `StoreIndex`, `DimArray`, void `Call` /
  `CallRuntime`) no longer allocate unused result temps; `IrDump` prints a `%N =`
  binding only when `producesValue` holds.
- Golden `.tbc` fixtures under `tests/fixtures/golden/` lock the full emit text
  (`hello`, `count`, `arith`, `arrays`, `reals`, `subprograms`). Regenerate with
  `APOLLO_UPDATE_GOLDEN=1`.

**Sequencing:** 1a and 1b were independent of each other. 1c went **last** — it reshapes
nearly every emitted array sequence, so goldens added before the semantics settled would
have been rewritten twice.

**Status:** completed — **1a**, **1b**, and **1c** landed.

### Stage 2 — Records (M7b)

**Objective:** Classic Wirth structured data **in memory**.

- Parse / analyse `record` … `end` and field selection.
- Lower and emit field load/store (document layout / mangling).
- Whole-record assign if straightforward; otherwise elementwise / diagnose clearly.
- Array parameters and whole-array assignment via `MAT_COPY` (moved here from Stage 1,
  which only diagnoses them — see D4).
- Not a commitment to `file of record` in M7 — see **Why Pascal `file` I/O waits** above.

**Layout / lowering (pinned):**

- Record variable `p` with fields `x`, `y` → scalar slots `fn$p$x`, `fn$p$y` (same `$`
  separator as locals; no VM aggregate).
- Array record fields dimension `fn$p$field` like ordinary array locals.
- Whole-record assign `q := p` copies fields pairwise; array fields use `MAT_COPY` and
  require matching bounds (structural assignability + `arrayTypesSameShape` per array field).
- Whole-array assign `a := b` emits `MAT_COPY dst|src`.
- **Value array parameters (calling convention):** before `CALL`, the caller emits
  `DIM_ARRAY` formal → `MAT_INIT` formal → `MAT_COPY formal|actual` (Gemini requires the
  destination array to exist, and re-dimming wipes contents). The callee does **not**
  `DIM_ARRAY` / `MAT_INIT` array parameters in its entry prologue (locals only). Array
  formals are still skipped by the scalar param `STORE_VAR` prologue. Names use declaration
  spellings (`Symbol::name` / param and local declared names), not call-site identifier case.

**Explicit non-goals (Stage 2):** nested record field types; `var` array parameters; record
parameters; record or array function results; `file of record`.

**Status:** completed.

### Stage 3 — `case` + nesting polish (M7c)

**Objective:** Control-flow and subprogram completeness for console programs.

- `case` … `of` … `end` on integer/char (and boolean if cheap).
- Nested subprograms: shallow up-level locals or a frozen “top-level only” diagnostic
  policy documented in Stage notes.

**Case notes (pinned):**

- Selector types: `integer`, `char`, `boolean`. Reject `real`, string, arrays, records.
- Labels: constants (literals or named consts), comma lists, and `lo..hi` ranges (not for
  boolean). Overlapping labels are diagnosed.
- Optional `else`; **no fall-through** between arms; no match and no else continues after
  `end`.
- Lowering: evaluate the selector once, then a linear `CmpEq` / range (`CmpGe`∧`CmpLe`)
  compare chain with `BranchIf` into arm blocks and a shared `case.merge` (no jump table).

**Nesting policy (frozen):**

- Only procedures/functions declared in the **program block** lower to IR functions.
- Nested subprograms inside a subprogram are diagnosed at analyse time.
- Access to enclosing-scope `var`/`param` from a subprogram is diagnosed at analyse
  (and still at lower if reached). No activation records or `main$` capture in Stage 3.
- Historical IR notes: [04-intermediate-representation.md](04-intermediate-representation.md).

**Status:** completed.

### Stage 4 — `{$I}` and dialect close-out (M7d)

**Objective:** Multi-file sources without units; freeze the supported dialect.

- Directive-aware `{`$…`}` in the scanner; implement `{$I}` / `{$i}` include stack
  (cycle detection, depth limit, path relative to includer).
- Unknown directives: diagnose or warn (pick one; document it).
- Dialect summary in README / this file; examples using includes.
- Version bump policy when closing: report **`0.7.0`** (`PROJECT_VERSION`); cut git tag
  `v0.7.0` as a separate release follow-up.

**Include notes (pinned):**

- Directives appear only in `{`$…`}` (not `(*…*)`). `{$I}` / `{$i}` take a filename
  (optional quotes); path is resolved relative to the **including** file’s directory.
- Include stack: cycle detection via weakly-canonical absolute paths; max nesting depth
  **32**; missing file / cycle / depth → **error**.
- Unknown `{$…}` directives → **warning** (do not fail `--check` / `--emit` by themselves).
- `--list` does not expand includes. Tag `v0.7.0` is a separate release step.

**Status:** completed.

---

## Success criteria (draft)

- [x] Array indexing programs with plain integer-literal bounds emit and run on `gemini-vm`.
- [x] Array indexing is correct for every accepted bound form, and array parameters are
  either supported or diagnosed (Stage 1 follow-up **1a**).
- [x] `real` and `mod` no longer hard-fail in codegen for the supported subset.
- [x] `real` arithmetic is numerically faithful (Stage 1 follow-up **1b**).
- [x] Flat `record` field access emits and runs.
- [x] `case` on ordinal types emits and runs.
- [x] `{$I}` includes compose a multi-file program that `--emit`s cleanly.
- [x] README documents the supported Wirth console / Pascal80-style dialect and explicit
  non-goals (units, files, pointers, sets, …).
- [x] Milestone 8 remains deferred; no second language front-end started as part of M7.
- [x] `ctest` green on a clean configure/build.

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Scalar / array debt | completed |
| Stage 2 — Records | completed |
| Stage 3 — `case` + nesting | completed |
| Stage 4 — `{$I}` + dialect close-out | completed |

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
- Sets, pointers, and **file I/O** (after Gemini’s shared filesystem façade — see
  **Why Pascal `file` I/O waits** above).
- Richer directives and range-check pragmas.
- Switch console builtins from `PRINT_*` to `CALL_FUNC` + Pascal module when Gemini’s
  module path is ready.
- [Milestone 8 — Multi-language expansion](08-multi-language-expansion.md) once Pascal is
  the reference front-end.
