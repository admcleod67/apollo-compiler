← [Project milestones index](../milestones.md)

## Milestone 5 — Code generator

This document defines the **Milestone 5** Gemini code generator for Apollo: walk the
Milestone 4 shared IR and emit **Gemini `.tbc` text bytecode** that the existing PickVM
parser and runtime can load, so `examples/hello.pas` and `examples/count.pas` become
runnable programs on Gemini (and later on Milestone 6’s standalone runner).

It complements:

- [Project milestones](../milestones.md) (Milestone 5 scope)
- [Overview](../overview.md) (pipeline and `.tbc`-first integration)
- [Milestone 4 — Intermediate representation](04-intermediate-representation.md) (IR to consume)
- [Milestone 6 — Standalone VM execution](06-standalone-vm-execution.md) (completed standalone
  runner consumer)
- Sister project **gemini-system** — bytecode VM docs (`docs/vm.md`) and language-module ABI

### Goals

- Emit **Gemini `.tbc` text** from `apollo::ir::Module` — no hard build-time link against
  `gemini-core` / PickVM in this milestone (loose coupling; Gemini loads the file).
- Cover the **v1 IR subset** already produced for `examples/hello.pas` and
  `examples/count.pas` (literals, locals, arith/compare, `CallRuntime` console I/O,
  multi-block CFGs, top-level user `Call` / `Return`).
- Define an explicit **IR → opcode** mapping and a **calling convention** for params,
  locals, and function results on Gemini’s stack + variable map.
- Bind Pascal console builtins (`write` / `writeln` / `read` / `readln`) through a
  documented runtime binding table (see **Console I/O binding** below) — not ad-hoc
  Pick-specific opcodes scattered through the emitter.
- Expose emission from the driver (`apolloc --emit` / `--tbc`); keep `--ir` as the
  inspect-IR path.
- Leave standalone host packaging, language-module `.so` shipping, Pascal language
  completeness, and multi-language front-ends out of this milestone (Milestones 6–8).
- Leave `main` green after each stage (`cmake` build + `ctest`).

### Language / codegen scope (v1)

Aligned with what Milestone 4 already lowers cleanly:

**In scope**

- Emit a single `.tbc` image per compilation unit (module → one program text).
- Map IR functions (`main` + top-level subprograms) to labeled regions with
  `CALL` / `RETURN` (or an equivalent documented convention).
- Map locals/params to Gemini `STORE_VAR` / `LOAD_VAR` (mangled names; see calling
  convention).
- Map temporaries via the operand stack (post-order emission; stack discipline).
- Map arithmetic, unary, comparisons, and branches (`JUMP` / `JZ`) for the integer /
  boolean subset used by hello/count.
- Map `CallRuntime` for console I/O per the binding table.
- Driver flag that writes `.tbc` to stdout or a file; golden / substring tests on
  emitted text for hello and count.

**Explicitly deferred**

- In-process PickVM linking / emitting `Instruction` vectors in C++ (optional later;
  M5 prefers text `.tbc` only).
- True activation records / nested-frame / closure support (M4 already diagnoses
  enclosing-scope access and deep nesting).
- Full `var` call-by-reference write-back (M4 models `var` params as ordinary slots).
- Real/`F64` fidelity on the VM (Gemini’s stack is int/string today) — do not block
  hello/count; diagnose or document limited real support if a fixture forces it.
- Array index lowering (`DIM_ARRAY` / `LOAD_ARR` / `STORE_ARR`) — M4 arrays remain
  opaque `ArrayRef`.
- Shipping the Pascal I/O shared library (Milestone 6 ownership; see language-module note).
- SSA, register allocation, peephole optimization beyond what’s needed for readable
  correct `.tbc`.

### Milestone slices (summary)

| Slice | Focus |
|-------|--------|
| **M5a** | Codegen skeleton + `.tbc` writer + hand-built IR→`.tbc` fixture |
| **M5b** | Straight-line emission (consts, locals, arith, console `CallRuntime`) |
| **M5c** | CFG branches/loops + user calls / calling convention |
| **M5d** | Driver `--emit` / `--tbc`, golden hello/count, M5 finish / `0.5.0` |

Detailed staged delivery is below. Slices map 1:1 to Stages 1–4.

**Method note:** Mak’s backend thickens after a working intermediate form. Apollo mirrors
that with original code: first a real `.tbc` writer and tiny fixture, then straight-line
programs (hello), then control flow and subprograms (count + procedures), then expose
emission from `apolloc` and freeze the contract for Milestone 6.

---

## Design targets (not frozen APIs)

### Shared vs Pascal-local codegen

| Choice | Default for M5 |
|--------|----------------|
| Ownership | Prefer shared `apollo::codegen` (or `apollo-codegen`) that consumes `apollo::ir` only |
| Pascal-specific | Runtime name → opcode / `CALL_FUNC` binding for `CallRuntime` text (`writeln`, …) |
| Existing placeholder | `src/pascal/codegen/.gitkeep` — replace or relocate once Stage 1 notes fix the split |
| Gemini coupling | Emit **text** opcodes from `gemini-system/docs/vm.md`; do not `#include` PickVM headers in M5 |

### IR → Gemini opcode mapping (v1 intent)

| IR | Gemini `.tbc` (intent) |
|----|------------------------|
| `ConstI32` / `ConstBool` / `ConstChar` | `PUSH_INT` (bool as 0/1; char as code point) |
| `ConstString` | `PUSH_STR "..."` (escape per Gemini rules) |
| `ConstF64` | `PUSH_FLT` (Milestone 7 Stage 1) |
| `LoadLocal` / `StoreLocal` (local or param slot) | `LOAD_VAR` / `STORE_VAR` with mangled name |
| `Add`/`Sub`/`Mul`/`Div`/`Mod` | `ADD`/`SUB`/`MUL`/`DIV`; `Mod` → `a-(a div b)*b` sequence (M7a) |
| `Neg` / `Not` | Arithmetic / logical sequences on ints |
| `And` / `Or` | Integer 0/1 bitwise-style sequences (document exact form in Stage 2 notes) |
| `Cmp*` | `EQ`/`NE`/`LT`/`LE`/`GT`/`GE` |
| `Call` (user) | `CALL <label>` after pushing args per calling convention |
| `CallRuntime` | Binding table → `PRINT_*` / `INPUT_*` / `PRINT_EOL` (v1) or `CALL_FUNC` (when published) |
| `Branch` | `JUMP label` |
| `BranchIf` | `JZ` / inverted-JZ sequence (document stack sense: Gemini `JZ` jumps on **zero**) |
| `Return` | `RETURN` (non-`main`); `main` ends with `HALT` |

Exact sequences are frozen in Stage notes as each stage lands; this table is the target shape.

### Calling convention (v1)

Gemini’s runtime variable map is **global** (no hardware frames). Apollo’s v1 convention:

- Mangle locals/params as `<function>$<name>` (or similar) so `main` and `Bump` do not collide.
- **Incoming params:** caller pushes args in order; callee `STORE_VAR`s into mangled param
  slots at entry (or caller stores into callee’s mangled names before `CALL` — pick one in
  Stage 3 notes and stick to it).
- **Function result:** callee leaves the return value on the stack (or stores to a mangled
  `$result` then loads before `RETURN`); caller consumes the stack value after `CALL`.
- **`main`:** no `CALL` from outside; body runs from a documented entry label (e.g. `main:`)
  and ends with `HALT` (not `RETURN`).
- **`var` parameters:** still ordinary slots (no write-back to caller) — same limitation as
  M4; document again in Stage 3 notes.

### Console I/O binding

Apollo IR already uses `CallRuntime` with folded names `write` / `writeln` / `read` /
`readln`. Milestone 5 maps those names through a **binding table**:

| Horizon | Mechanism |
|---------|-----------|
| **M5 v1 (bootstrap)** | Emit opcodes that exist in today’s Gemini VM (`PRINT_VAL` / `PRINT_INT` / `PRINT_STR`, `PRINT_EOL`, `INPUT_INT` / `INPUT_STR`, etc.) so hello/count run without waiting on a new opcode. Prefer `PRINT_VAL` + `PRINT_EOL` for writeln-shaped calls where types vary. |
| **Steady state (with Gemini)** | Emit **`CALL_FUNC`** (or the published equivalent) with namespace/function IDs from Gemini’s module ABI, once that opcode and Pascal module IDs are published. Keep the binding table as the single switch point. |

Do **not** scatter Pick-specific I/O through the emitter. Ownership of the Pascal helper
**shared library** remains under
[Milestone 6 — Language modules](06-standalone-vm-execution.md#language-modules-pascal-builtins).

### Stack discipline

IR values are SSA-like temps in a CFG without φ-nodes. The emitter should evaluate
expressions in order so each non-void instruction’s result is on the Gemini operand stack
(or immediately `STORE_VAR`’d when the IR would otherwise need the value across a block
boundary). Values live across blocks only via locals/params (`STORE_VAR` before branch,
`LOAD_VAR` after) — matching how M4 already materializes loop controls and assigns.

---

## Staged delivery plan

Work lands in four mergeable stages. Each stage should leave `main` green and update the
**Implementation status** section when closed.

### Stage 1 — Codegen skeleton & `.tbc` writer (M5a)

**Objective:** Land a codegen library and textual `.tbc` emitter with **no** (or only a
tiny hand-built) IR walk. Establish headers, CMake target, and dump/write helpers.

**Deliverables**

- Public headers under `include/apollo/codegen/` (preferred) **or**
  `include/apollo/pascal/codegen/` if a shared tree is deferred — **prefer shared** so
  later languages reuse the emitter against `apollo::ir`.
- Implementation under `src/codegen/` (new `apollo-codegen` target) and/or replace
  `src/pascal/codegen/.gitkeep` once the shared/pascal split is fixed in Stage 1 notes.
- A writer API that accepts lines / labeled instructions and produces Gemini-compatible
  `.tbc` text (labels, opcodes, `#` comments, `PUSH_STR` escaping).
- Unit test: hand-constructed emission matching a tiny golden string (e.g. push string +
  print + eol + halt), without requiring Pascal front-end.
- CMake: link `apollo-codegen` as needed; do not break `apolloc` yet (no new CLI in
  Stage 1).

**Out of scope for Stage 1**

- Walking `apollo::ir::Module`.
- Control flow or calling convention.
- `--emit` CLI (Stage 4).

**Acceptance criteria**

- [x] Codegen library builds and a writer unit test passes.
- [x] Documented `.tbc` formatting conventions live in this file (Stage 1 notes).
- [x] Existing M1–M4 tests remain green.

**Stage 1 notes**

- Library: `apollo-codegen` in `include/apollo/codegen/` + `src/codegen/` (`TbcWriter.hpp`,
  `TbcWriter.cpp`), alias `apollo::codegen`. No dependency on `apollo-ir` or
  `apollo-pascal` yet — Stage 1 is a pure text writer.
- **Shared vs Pascal:** IR→`.tbc` emission (Stage 2+) lives in shared `apollo-codegen`
  because IR is language-neutral. `src/pascal/codegen/.gitkeep` remains as a placeholder
  for any future Pascal-only binding helpers; it is not the home of the emitter.
- **Formatting:** labels at column 0 as `name:`; instructions indented with four spaces
  (matching `gemini-system/programs/hello.tbc`). Comments are `# …` lines.
- **API:** `TbcWriter::comment`, `label`, `op(opcode)` / `op(opcode, operand)`,
  `pushInt`, `pushStr`; `str()` / `write(ostream)`. Opcode strings only — no opcode enum
  until Stage 2 maps IR.
- **`PUSH_STR` escapes:** `escapeTbcString` encodes `\\`, `\"`, `\n`, `\r`, `\t` per
  Gemini’s `.tbc` parser rules.
- **Out of scope still:** no `apollo::ir` walk, no `--emit` CLI, no PickVM link.

**Status:** completed.

### Stage 2 — Straight-line IR emission (M5b)

**Objective:** Emit `.tbc` for straight-line IR: constants, local load/store, arithmetic /
compare, and console `CallRuntime`, so a lowered `examples/hello.pas` (and similar)
produces runnable bytecode.

**Deliverables**

- `emitTbc(const ir::Module &) → string` (or stream writer) walking `main`’s blocks in
  order for the **single-block / straight-line** subset.
- Implement the IR → opcode mapping for consts, `LoadLocal`/`StoreLocal`, binary/unary
  ops needed by hello-shaped programs, and the console binding table.
- Unit / integration tests: lower+emit hello (or a synthetic straight-line fixture);
  assert substrings (`PUSH_STR`, `PRINT_`, `HALT`). Optionally smoke-run under Gemini’s
  VM if available in the developer environment — **not** required as a CMake dependency.
- Document stack discipline and any temporary spilling via `STORE_VAR`.

**Out of scope for Stage 2**

- `Branch` / `BranchIf` / multi-block CFGs.
- User `Call` / non-`main` functions.
- Driver CLI.

**Acceptance criteria**

- [x] A clean straight-line / hello-shaped module emits `.tbc` with zero diagnostics.
- [x] Emitted text contains expected console and halt opcodes.
- [x] M4 IR tests remain green.

**Stage 2 notes**

- **API:** `apollo::codegen::emitTbc(const ir::Module &, DiagnosticEngine &) → string` and
  `writeTbc` in `include/apollo/codegen/Emit.hpp` + `src/codegen/Emit.cpp`.
  `apollo-codegen` now PUBLIC-links `apollo::ir` and `apollo::common`.
- **Gate:** Stage 2 emits only when the module has exactly one function named `main` with
  exactly one block whose terminator is `Return`. Multi-block CFGs, extra functions, or
  `Op::Call` report an Error diagnostic and produce no `.tbc` (Stage 3).
- **Labels / halt:** entry label is `main:`; `main`'s `Return` becomes `HALT` (not
  `RETURN`).
- **Mangling:** locals/params → `STORE_VAR` / `LOAD_VAR` names `<function>$<decl.name>`
  (e.g. `main$x`). Spill slots for IR temps → `main$t<N>`.
- **Stack discipline:** after each value-producing instruction the result is **eagerly
  spilled** to `main$t<N>` so later operands can always be reloaded. Binary ops emit
  `LOAD_VAR` left, `LOAD_VAR` right, then the Gemini opcode (stack `[left, right]`).
  `ensureOnTop` reloads a spilled temp before `STORE_VAR` / `PRINT_VAL`.
- **Unary / logic:** `Neg` → `PUSH_INT 0` + load + `SUB`; `Not` → load + `PUSH_INT 0` +
  `EQ`; `And` → `MUL` on 0/1; `Or` → `ADD` then `NE` 0 (nonzero → 1).
- **Console binding (v1):** `write` / `writeln` → per-arg `PRINT_VAL` (+ `PRINT_EOL` for
  writeln); `read` / `readln` → `INPUT_INT` or `INPUT_STR` by result `IrType` (M4 emits
  one runtime call per variable).
- **Unsupported in Stage 2:** `ConstF64`, `Mod`, `Copy`, `Call`, branches — diagnosed.

**Status:** completed.

### Stage 3 — Control flow & calling convention (M5c)

**Objective:** Emit multi-block CFGs and top-level subprograms so `examples/count.pas`
(and procedure/function fixtures) become well-formed `.tbc`.

**Deliverables**

- Map IR block labels to `.tbc` labels; `Branch` → `JUMP`; `BranchIf` → `JZ` (with
  documented polarity / inversion).
- Emit `for` / `while` / `repeat` / `if` shapes already produced by M4 (no IR changes
  required unless a bug is found).
- Implement the v1 calling convention for user `Call` / `Return` and function results;
  mangle locals/params per function.
- Tests: count.pas emission (labels + compare + jump back); a one-level procedure /
  function fixture; regression that hello still emits cleanly.

**Out of scope for Stage 3**

- Nested subprogram frames / enclosing-scope access (still unsupported upstream).
- True `var` write-back.
- Driver CLI (Stage 4).

**Acceptance criteria**

- [x] `examples/count.pas` emits `.tbc` with zero diagnostics and visible loop jumps.
- [x] A top-level subprogram fixture emits a callable region and a `CALL` from `main`.
- [x] Existing tests remain green.

**Stage 3 notes**

- **Module walk:** `emitTbc` emits every `module.functions` entry in order (`main` first per
  M4). Gate is only “non-empty module with `functions[0].name == main`”.
- **Block labels:** entry block (`entry`) → `.tbc` label `<function>:`; other IR labels →
  `<function>$<irLabel>` (e.g. `main$for.head.0`). Branch targets use the same mangling.
- **Block boundaries:** `stackTop` is cleared at each block start; spilled temps and
  locals/params survive across blocks via `STORE_VAR` / `LOAD_VAR`.
- **Terminators:** `Branch` → `JUMP`; `BranchIf` → `JZ <false>` then `JUMP <true>` (Gemini
  `JZ` jumps on zero; M4 conditions are 0/1); `main` `Return` → `HALT`; subprogram
  `Return` → leave optional return value on stack, then `RETURN`.
- **Calling convention:** caller pushes args left-to-right (`LOAD_VAR` each spilled temp)
  then `CALL <name>`. Callee entry prologue stores params in **reverse** order into
  `<fn>$<paramName>`. Function results stay on the stack through `RETURN`; the caller’s
  `Op::Call` result temp is spilled like any other value. `var` params remain ordinary
  value slots (no write-back).
- **Still diagnosed:** `Copy` (unused). Milestone 7 Stage 1 emits `ConstF64` → `PUSH_FLT`
  and `Mod` as a `DIV`/`MUL`/`SUB` remainder sequence; arrays use `DIM_ARRAY` /
  `LOAD_ARR` / `STORE_ARR` with Pascal `[lo..hi]` remapped to 1-based VM indices.

**Status:** completed.

### Stage 4 — Driver, polish & Milestone 5 finish (M5d)

**Objective:** Expose emission from `apolloc`, polish docs/README, and close Milestone 5.

**Deliverables**

- `apolloc --emit <file>` (short `-e`) and/or `--tbc <file>`: load → scan → parse →
  analyse → lower → emit `.tbc` to stdout (or `-o` file if added); diagnostics on stderr;
  non-zero exit on errors; **no `.tbc` on stdout when errors are present** (same policy as
  `--ir`).
- README documents `--emit` / `--tbc`.
- Golden or substring CLI tests for hello and count; error fixture `WILL_FAIL`.
- Update this document’s **Implementation status** and definition of done.
- Version / tag policy: toolchain reports **`0.5.0`** when closing (root
  `PROJECT_VERSION` may already be bumped; cut git tag `v0.5.0` as a separate release
  follow-up).

**Acceptance criteria**

- [x] `--emit` / `--tbc` on `examples/hello.pas` and `examples/count.pas` exits 0 and
  prints `.tbc`.
- [x] `--emit` on a semantic-error fixture exits non-zero (no bytecode dump required).
- [x] README documents `--list`, `--tokens`, `--ast`, `--check`, `--ir`, and `--emit`.
- [x] All Stage 1–4 tests pass under `ctest`.

**Stage 4 notes**

- **CLI:** `apolloc --emit` / `-e` and alias `--tbc` run load → scan → parse → analyse →
  `lowerToIr` → `emitTbc` → stdout. Diagnostics go to stderr. The `.tbc` dump is written
  **only** when `errorCount() == 0` after analyse+lower+emit (parse failure skips
  lowering entirely), so semantic-error fixtures exit non-zero with an empty stdout for
  bytecode.
- **CMake:** `apolloc` PRIVATE-links `apollo::codegen` (not transitive via `apollo-pascal`).
- **CLI tests:** `apolloc_emit_hello`, `apolloc_emit_count`, `apolloc_emit_error`
  (`WILL_FAIL`), and `apolloc_tbc_hello` (alias coverage).
- **Version:** toolchain already reports `0.5.0` (`PROJECT_VERSION`). Cutting git tag
  `v0.5.0` is a separate release follow-up, not part of this stage's code change.
- **Console binding (v1, documented):** `write` / `writeln` → `PRINT_VAL` (+ `PRINT_EOL`);
  `read` / `readln` → `INPUT_INT` / `INPUT_STR`. Steady-state switch to `CALL_FUNC` remains
  a Gemini follow-on; see **Console I/O binding** above.

**Status:** completed.

### Suggested staging cadence

1. **Stage 1** — `.tbc` writer + library wiring.
2. **Stage 2** — straight-line emission; hello-shaped `.tbc`.
3. **Stage 3** — CFG + calling convention; count + subprograms.
4. **Stage 4** — `--emit`, close-out / `0.5.0`.

Do not start Milestone 6 standalone packaging work as a hard dependency of M5 — M5 can
prove bytecode against Gemini’s existing VM / `.tbc` loader. M6 consumes M5 output.

---

## Component layout

| Area | Location |
|------|----------|
| Shared codegen headers | `include/apollo/codegen/` (preferred) |
| Shared codegen impl | `src/codegen/` → `apollo-codegen` |
| Pascal-local placeholder | `src/pascal/codegen/.gitkeep` — replace or redirect in Stage 1 |
| IR consumed | `apollo::ir` (`include/apollo/ir/`) |
| Driver | `src/tools/apolloc.cpp` (`--emit` / `--tbc` in Stage 4) |
| Tests | `tests/` (writer unit + emit integration + CLI) |
| Examples | `examples/hello.pas`, `examples/count.pas` (must `--emit` clean at close-out) |
| Gemini reference | `gemini-system/docs/vm.md`, `gemini-system/programs/*.tbc` |

---

## Test matrix

| Stage | Minimum coverage |
|-------|------------------|
| 1 | Hand-built `.tbc` writer output; library links |
| 2 | Straight-line / hello IR → `.tbc` substrings |
| 3 | count.pas loops; subprogram `CALL`; hello regression |
| 4 | `--emit` exit 0 / non-zero; README; version `0.5.0` |

---

## Boundary with adjacent milestones

| Concern | Milestone 4 | Milestone 5 | Milestone 6 |
|---------|-------------|-------------|-------------|
| Shared IR | Owns | Consumes | Does not see IR |
| `.tbc` / Gemini opcodes | No | Owns emission | Loads / runs |
| Console binding table | `CallRuntime` names | Maps to opcodes / `CALL_FUNC` | Hosts Pascal I/O module |
| `--ir` | Yes | Still valid | Still valid |
| `--emit` / `--tbc` | No | Yes (Stage 4) | Consumers of output |
| Standalone runner | No | No | Yes (Gemini-side) |

---

## Runtime / language-module note

Console builtins (`write` / `writeln` / `read` / `readln`) should **eventually** lower to
Gemini **`CALL_FUNC`** (or the published equivalent) using namespace/function IDs from
Gemini’s module ABI — not Pick-specific opcodes embedded ad hoc in the emitter.

Until that opcode and Pascal module IDs are published, Milestone 5’s **v1 binding table**
may emit the existing console opcodes documented in `gemini-system/docs/vm.md`
(`PRINT_VAL`, `PRINT_EOL`, `INPUT_INT`, …) so hello/count are demonstrably runnable.
Switching the table to `CALL_FUNC` later must not require revisiting Pascal AST or IR
shapes — only the binding rows.

Ownership of the Pascal helper **shared library** (spike in gemini-system vs steady
state built with Apollo) is documented under
[Milestone 6 — Language modules](06-standalone-vm-execution.md#language-modules-pascal-builtins).

---

## Open debts / prerequisites (from M4)

Carried into codegen awareness (not all must close in M5):

- **Enclosing-scope locals / deep nesting** — still diagnosed at lower time; codegen
  assumes flat top-level functions only.
- **`var` write-back** — not modeled; treat as value slots.
- **Array element access** — not in IR yet; no `DIM_ARRAY` emission required for v1
  examples.
- **Real/`F64`** — VM stack is int/string; keep hello/count (integer) as the success bar.

---

## Scope and non-goals

### In scope (M5)

- Shared (preferred) IR → `.tbc` emitter for the checked v1 subset.
- Calling convention + console binding table.
- `apolloc --emit` / `--tbc` at close-out.
- Runnable hello/count bytecode on Gemini’s existing `.tbc` loader.

### Deferred (not in this milestone)

- Standalone host VM packaging (Milestone 6).
- Shipping Pascal `.so` language module as Apollo’s product (M6 steady state).
- Pascal Wirth console completeness (Milestone 7).
- Additional source languages (Milestone 8, deferred).
- Heavy optimization / register allocation.
- In-process PickVM link as the primary deliverable.

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Codegen skeleton & `.tbc` writer | completed |
| Stage 2 — Straight-line IR emission | completed |
| Stage 3 — Control flow & calling convention | completed |
| Stage 4 — `--emit`, close-out | completed |

---

## Definition of done (Milestone 5)

- [x] Stages 1–4 acceptance criteria checked off.
- [x] `ctest` green on a clean configure/build.
- [x] README documents `apolloc --emit` (or `--tbc`).
- [x] `examples/hello.pas` and `examples/count.pas` pass `--emit`.
- [x] This status table marked completed.
- [x] Version policy recorded (reports `0.5.0`; cut git tag `v0.5.0` as a separate
  release follow-up).
- [x] Console binding table documented (v1 opcodes; `CALL_FUNC` IDs deferred to Gemini).

---

## Migration / follow-on notes

- Milestone 6 should run Apollo-emitted `.tbc` only — avoid reaching back into Pascal AST
  or IR from the runner.
- Prefer stable `.tbc` shape early; treat emission churn after Stage 3 as a soft contract
  break for golden tests.
- Keep Gemini opcode details out of `apollo::ir`; binding belongs in codegen.
- When codegen docs outgrow this page, split to `docs/codegen.md` and link it from here.
