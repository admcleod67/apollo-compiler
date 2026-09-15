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
| Array **indexing** `a[i]` | Stage 1: runs for plain literal bounds; follow-up **1a** open |
| `real` / `mod` through emit | Stage 1: `PUSH_FLT` / mod sequence; follow-up **1b** open |
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
- [ ] `real` arithmetic is faithful: literal precision round-trips and `Integer → Real`
  widens before real division (follow-up **1b**).
- [x] Integer `mod` emits without a language module.
- [x] Indexed array load/store emit `DIM_ARRAY` / `LOAD_ARR` / `STORE_ARR`.
- [ ] Every accepted array bound form lowers correctly (follow-up **1a**).
- [x] Stage fixtures pass under `ctest`.

**Stage 1 notes**

- **`real`:** `Op::ConstF64` → Gemini `PUSH_FLT`. Binary/`Neg` reuse `ADD`/`SUB`/… (VM
  `Value` is `int|double|string`).
- **`mod`:** Gemini has no core `MOD` opcode. Emit `a - (a div b) * b` with `DIV` /
  `MUL` / `SUB` (toward-zero `DIV`). Do not call BASIC `CALL_FUNC` MOD.
- **Arrays:** const `[lo..hi]` bounds stored on `Type`; IR `DimArray` / `LoadIndex` /
  `StoreIndex`; emit remaps Pascal index to VM 1-based via `i - lo + 1`, then
  `DIM_ARRAY` + zero-init loop + `LOAD_ARR`/`STORE_ARR`. Whole-array assignment still
  diagnosed.
- **Smoke:** `apolloc --emit <file> | ../pick-system/build/src/gemini-vm /dev/stdin`
  (no CMake dependency on pick-system).

**Semantics pinned in Stage 1**

- `mod` uses truncated division, so `-7 mod 2` is `-1` and `7 mod -2` is `1`. That matches
  Turbo Pascal, **not** ISO / Wirth (which yields `1` and treats a non-positive divisor as
  an error). Turbo compatibility is the intended choice for this dialect.
- Array bounds checking comes from the VM, which reports mangled slot names
  (`Array index out of bounds: MAIN$A`). Apollo emits no range check of its own.
- Nothing validates that bounds or element counts fit the VM's 32-bit `int`; oversized
  `PUSH_INT` operands fail at `.tbc` load time (see follow-up **1a**).

**Stage 1 review findings (open follow-ups)**

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

Follow-ups land as three separate plans, each independently green and mergeable.

**1a — Array bound plumbing (D1, D4)**

- Carry the analysed bounds forward to lowering (resolved type, or bounds recorded on
  `ast::TypeDenoter`) instead of re-deriving them; treat missing bounds as a hard internal
  error rather than a `1..1` default.
- Consider replacing `Type::hasBounds` with optional bounds so the type cannot express a
  guess.
- Diagnose array-typed parameters until real support lands in Stage 2.
- Tests: negative low bound, parenthesised bound, const-named bound, and a bound declared
  inside a procedure.

**1b — Real numerics (D2, D3)**

- Emit round-trippable `PUSH_FLT` operands (`setprecision(17)` or shortest round-trip).
- Widen `Integer → Real` before real division and real assignment — real-typed integer
  literals as `PUSH_FLT`, an IR convert op, or emit-side coercion when the result type is
  `F64`.

**1c — Codegen quality and golden `.tbc` (no semantic change)**

- Replace the per-array zero-init loop with `PUSH_INT 0` + `MAT_INIT`, dropping ~15
  instructions, the `$dim` / `$i` helper variables, and two labels per array — plus the
  intra-basic-block branching that currently makes emitted control flow diverge from the
  IR CFG. String arrays need nothing: `DIM_ARRAY` already fills with `""`.
- Fold `SUB lo` + `ADD 1` into a single `SUB (lo - 1)`, and omit it when `lo` is 1.
- Emit `mod` on the stack (`LOAD a; LOAD a; LOAD b; DIV; LOAD b; MUL; SUB`) instead of
  spilling `$quot` / `$prod`.
- Drop the unused `DimArray` / `StoreIndex` result temps and the unused `<sstream>`
  include in `Emit.cpp`.
- Add golden `.tbc` fixtures: today's tests assert only that opcode strings appear, which
  is why D1 escaped.

**Sequencing:** 1a and 1b are independent of each other. 1c goes **last** — it reshapes
nearly every emitted array sequence, so goldens added before the semantics settle would be
rewritten twice.

**Status:** completed — follow-ups **1a**–**1c** open.

### Stage 2 — Records (M7b)

**Objective:** Classic Wirth structured data **in memory**.

- Parse / analyse `record` … `end` and field selection.
- Lower and emit field load/store (document layout / mangling).
- Whole-record assign if straightforward; otherwise elementwise / diagnose clearly.
- Array parameters and whole-array assignment via `MAT_COPY` (moved here from Stage 1,
  which only diagnoses them — see D4).
- Not a commitment to `file of record` in M7 — see **Why Pascal `file` I/O waits** above.

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

- [x] Array indexing programs with plain integer-literal bounds emit and run on `gemini-vm`.
- [ ] Array indexing is correct for every accepted bound form, and array parameters are
  either supported or diagnosed (Stage 1 follow-up **1a**).
- [x] `real` and `mod` no longer hard-fail in codegen for the supported subset.
- [ ] `real` arithmetic is numerically faithful (Stage 1 follow-up **1b**).
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
| Stage 1 — Scalar / array debt | completed; follow-ups 1a–1c open |
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
- Sets, pointers, and **file I/O** (after Gemini’s shared filesystem façade — see
  **Why Pascal `file` I/O waits** above).
- Richer directives and range-check pragmas.
- Switch console builtins from `PRINT_*` to `CALL_FUNC` + Pascal module when Gemini’s
  module path is ready.
- [Milestone 8 — Multi-language expansion](08-multi-language-expansion.md) once Pascal is
  the reference front-end.
