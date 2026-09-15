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
- Second language front-ends targeting the IR (Milestone 8, deferred).

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

- [x] IR library builds and a dump-roundtrip-style unit test passes.
- [x] Documented opcode / operand conventions live in this file (Stage 1 notes).
- [x] Existing M1–M3 tests remain green.

**Stage 1 notes**

- Library: `apollo-ir` in `include/apollo/ir/` + `src/ir/` (`Ir.hpp`, `IrDump.hpp`,
  `Ir.cpp`, `IrDump.cpp`). Pascal lowerer stays in `src/pascal/ir/` from Stage 2.
- **IrType:** `I32`, `F64`, `Bool`, `Char`, `StringRef`, `ArrayRef`, `Void`, `Error`.
- **Values:** monotonic `%N` temporaries per `Function` (`ValueId` / `newTemp()`).
- **Dump format:** indented lines, e.g. `Module name`, `Function f -> i32`, `Block entry`,
  `  %0 = const.i32 42 : i32`, `  return %0`. Runtime calls use `call.runtime @writeln`.
- **Non-terminating ops:** `const.*`, `copy`, `load.local`, `store.local`, arithmetic,
  unary, comparisons, `call`, `call.runtime`.
- **Terminators:** `return`, `branch`, `branch.if`.

**Status:** completed.

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

- [x] A clean straight-line Pascal fixture lowers with zero diagnostics and a stable dump.
- [x] Builtin `writeln` with a string (hello-shaped) appears in IR in the chosen form.
- [x] M3 analyse tests remain green.

**Stage 2 notes**

- Location: `include/apollo/pascal/ir/Lower.hpp` + `src/pascal/ir/Lower.cpp`, exposing
  `apollo::pascal::ir::lowerToIr(const ast::Program &, const SymbolTable &,
  DiagnosticEngine &) → apollo::ir::Module` (const refs — lowering only reads M3 results).
  `apollo-pascal` now links `apollo::ir` publicly.
- **Module/Function shape:** `Module.name = program.name` (as spelled); the whole program
  block body lowers into one `Function` literally named `main` (`Void` return, one `entry`
  block). User-declared procedures/functions are **not** lowered yet — call sites still
  emit a `Call` instruction referencing the name; their bodies land in Stage 3.
- **Type mapping:** `Integer→I32`, `Real→F64`, `Boolean→Bool`, `Char→Char`, `String→StringRef`,
  `Array→ArrayRef`, `Error→Error` (`Alias` is peeled by `canonicalTag` first).
- **Locals vs consts:** `block.vars` become `Function.locals` with a lowerer-local
  `foldedName → slot` map for `LoadLocal`/`StoreLocal`. `SymbolTable::Symbol` has no value
  field, so `const` declarations are **inlined at each use** from a `foldedName → literal
  Expr*` map built from `block.consts` (fresh temp per use, no CSE); `true`/`false` are
  special-cased since they have no backing `ConstDecl`.
- **Quoted literal decoding:** the scanner discards its decoded string, so `Expr::text` for
  string/char literals is still raw source (`'It''s'`); `decodeQuotedLexeme` in `Lower.cpp`
  strips the outer quotes and un-doubles `''`.
- **New shared ops:** `Op::And` / `Op::Or` added to `apollo::ir` (dump `and`/`or`) — Stage 1
  omitted logical ops needed for Pascal boolean binaries. `Slash` and `Div` both map to
  `Op::Div`, disambiguated only by the result `IrType` (F64 vs I32).
- **Console I/O shape:** `write`/`writeln` lower to one `CallRuntime{text=foldedName}` per
  statement with all argument temps in `args`. `read`/`readln` lower to one `CallRuntime`
  **per variable argument** (no `args`), each immediately followed by a `StoreLocal` into
  that variable's slot (only `read` needs a destination per argument).
- **Unsupported constructs:** `if`/`while`/`repeat`/`for` report a diagnostic
  ("control flow not lowered until Milestone 4 Stage 3") and are skipped rather than
  asserting, so accidental use is visible but non-fatal.

**Status:** completed.

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

- [x] `examples/hello.pas` and `examples/count.pas` lower with zero diagnostics.
- [x] Loop / branch fixtures show distinct blocks (or documented structured form) in the dump.
- [x] Existing tests remain green.

**Stage 3 notes**

- **Scope-popping discovery:** `SymbolTable` pushes a scope per subprogram body and **pops
  it** once `walkSubprogram` returns (`src/pascal/semantic/Analyse.cpp`), so after
  `analyse()` returns, a subprogram's own params/locals are no longer safely queryable via
  `symbols.lookup()` — the identifier could even resolve to an unrelated symbol of the same
  name from an outer scope. Mitigation (no M3 changes): `lowerIdentifier` now checks the
  current function's own `resolveSlot` (params/locals) **before** falling back to
  `symbols.lookup()`, and treats a `Var`/`Param` symbol found *only* via the fallback (or no
  symbol at all) as an enclosing-scope access, not a hit. Types are re-resolved fresh from
  each param/local's `TypeDenoter` via a lowerer-local `resolveTypeDenoter` (duplicates the
  ~10-line logic of `Analyse.cpp`'s `resolveDenoter`, matching the existing precedent of
  duplicating `foldAsciiLower` per-TU), which still works because predefined types and
  program-level `type` aliases live in the retained top scope.
- **One-level subprogram nesting:** only subprograms declared directly in
  `program.block.subprograms` are lowered, each into its own flat `ir::Function` named after
  the Pascal spelling (matching the existing `Call.text` convention). A subprogram's own
  nested `subprograms` (two levels deep) report an Error diagnostic ("nested subprogram
  lowering not supported until a later stage") rather than being silently skipped.
- **No enclosing-scope variable access:** each function's `localSlots`/`paramSlots` are
  scoped to only that function's own declarations (see scope-popping note above). A body
  referencing a Var/Param outside its own scope (e.g. an outer program-level var) reports an
  Error diagnostic ("accessing enclosing scope locals not supported until a later stage")
  instead of guessing.
- **`var` parameters:** modeled as ordinary `ir::Param` slots (read/write via
  `LoadLocal`/`StoreLocal` with a `Param` operand). True call-by-reference write-back to the
  caller's storage is **not** modeled — deferred to Milestone 5's calling convention.
- **Function result convention:** a function's own name assigned inside its body (`F :=
  expr;`) updates a per-lowering `resultValue` (`ValueId`) instead of a `StoreLocal` (there
  is no local slot backing the function's own name). `resultValue` is seeded with a
  zero-value literal of the return type before lowering statements, and the function's
  final `Return` uses it. Bare use of the function's own name in an expression is
  unaffected — that's already always a fresh 0-arg recursive call per M3.
- **`LowerCtx.block` is an owned value, not a reference:** Stage 2 bound `block` to a single
  `entry` `BasicBlock`. Stage 3 needs to seal the current block (attach a terminator, push
  into `Function.blocks`) and open new ones repeatedly, so `LowerCtx` now owns the "current"
  `BasicBlock` by value; `sealBlock(ctx, terminator)` / `beginBlock(ctx, label)` replace
  direct block construction. Invariant: on entry/exit of `lowerStmt`, `ctx.block` is always
  an open block with no terminator yet.
- **CFG block-label convention:** `<construct>.<part>.<N>`, `N` from a per-function
  monotonic `blockCounter` (e.g. `if.then.0`, `while.head.1`, `for.body.2`).
  - `if`/`else`: `BranchIf cond, then, (else|end)` → `then` → `Branch end`; optional `else`
    → `Branch end`; continue into `end`.
  - `while`: `Branch head`; `head` evaluates the condition, `BranchIf cond, body, end`;
    `body` lowers the loop statement then `Branch head`; continue into `end`.
  - `repeat...until`: `Branch body`; `body` lowers the statements, evaluates the condition,
    `BranchIf cond, end, body` (loops while the condition is false); continue into `end`.
  - `for`: `StoreLocal control, start`; evaluate `limit` once before the loop; `Branch
    head`; `head` reloads `control`, compares (`CmpLe`/`CmpGe` per `forDownto`) against
    `limit`, `BranchIf cond, body, end`; `body` lowers the loop statement, reloads
    `control`, `Add`/`Sub` by `1`, `StoreLocal`, `Branch head`; continue into `end`.
  - `case` (M7 Stage 3): evaluate selector once; `case.test.i` builds a Bool match
    (`CmpEq` / range via `CmpGe`∧`CmpLe`, `Or` across labels), `BranchIf` → `case.arm.i` or
    next test / `else` / `merge`; each arm `Branch` → `case.merge`; optional `else`.
- **Subprogram lowering:** a shared `lowerFunctionCore(symbols, diagnostics, name,
  returnType, params, block, resultName) -> ir::Function` is used for both `main` (`params =
  nullptr`, `resultName = nullopt`) and each top-level subprogram (`resultName` set iff
  `sub.isFunction`); `lowerToIr` lowers `main` then loops `program.block.subprograms`.
- **Call-site return type polish:** statement-context user calls (`lowerCallStmt`) now look
  up the real callee return type via `symbols.lookup(name)` instead of hardcoding `Void`,
  since callee bodies are now lowered.

**Status:** completed.

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

- [x] `--ir` on `examples/hello.pas` and `examples/count.pas` exits 0 and prints IR.
- [x] `--ir` on a semantic-error fixture exits non-zero (no IR dump required).
- [x] README documents `--list`, `--tokens`, `--ast`, `--check`, and `--ir`.
- [x] All Stage 1–4 tests pass under `ctest`.

**Stage 4 notes**

- **CLI:** `apolloc --ir` / `-i` runs load → scan → parse → analyse → `lowerToIr` →
  `writeIrDump` to stdout. Diagnostics go to stderr. The IR dump is written **only** when
  `errorCount() == 0` after analyse+lower (parse failure skips lowering entirely), so
  semantic-error fixtures exit non-zero with an empty stdout for IR.
- **Recursive array assignability:** `isAssignable` in `Analyse.cpp` now peels aliases via
  a local `peelAliases(TypePtr)` helper (preserving `Type::element`, unlike
  `canonicalTag`) and, when both sides are `Array`, recurses on element types. Same-tag
  and Integer→Real rules are unchanged. Static index-bound equality remains deferred.
  Analyse tests cover compatible (`array of integer` := `array of integer`) and
  incompatible (`array of integer` := `array of real`) assignments.
- **Arrays in IR:** assignability is correct for whole-array assignment, but arrays still
  lower only as opaque `ArrayRef` slots — no index load/store or bounds checks yet
  (Milestone 5+ if fixtures demand it).
- **Version:** toolchain already reports `0.4.0` (`PROJECT_VERSION`). Cutting git tag
  `v0.4.0` is a separate release follow-up, not part of this stage's code change.

**Status:** completed.

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

| `IrType` | M3 / use |
|----------|----------|
| `I32` | Integer |
| `F64` | Real |
| `Bool` | Boolean |
| `Char` | Char |
| `StringRef` | String / string literals |
| `ArrayRef` | Array (opaque ref for v1) |
| `Void` | Procedures / terminators |
| `Error` | Poison / recovery |

### Console I/O

Lower `write` / `writeln` / `read` / `readln` as runtime calls with a fixed Apollo runtime
name mangling (e.g. `@writeln`) so Milestone 5 can bind them to Gemini host ops without
revisiting Pascal AST.

---

## Open debts / prerequisites (from M3)

**Recursive array assignability** — **resolved in Stage 4.**

`isAssignable` previously treated any two `TypeTag::Array` values as assignable. It now
peels aliases and recurses on `Type::element`. Static index-bound equality can wait until
bounds matter for IR layout / sizing. Array *lowering* remains opaque (`ArrayRef` only).

See Stage 4 notes above and the cross-link under Milestone 3
[Migration / follow-on notes](03-semantic-analysis.md#migration--follow-on-notes).

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
- Additional source languages (Milestone 8, deferred).
- Pascal Wirth console completeness (Milestone 7).
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
| Stage 1 — Shared IR model & dump | completed |
| Stage 2 — Straight-line Pascal lowering | completed |
| Stage 3 — Control flow & subprograms | completed |
| Stage 4 — `--ir`, close-out | completed |

---

## Definition of done (Milestone 4)

- [x] Stages 1–4 acceptance criteria checked off.
- [x] `ctest` green on a clean configure/build.
- [x] README documents `apolloc --ir`.
- [x] `examples/hello.pas` and `examples/count.pas` pass `--ir`.
- [x] This status table marked completed.
- [x] Version policy recorded (reports `0.4.0`; cut git tag `v0.4.0` as a separate
  release follow-up).
- [x] Recursive array assignability fixed (Stage 4 notes); array IR lowering remains
  opaque `ArrayRef`.

---

## Migration / follow-on notes

- Milestone 5 should walk IR only — avoid reaching back into Pascal AST.
- Prefer stable dump formats early; treat dump churn after Stage 3 as a soft API break for
  golden tests.
- Keep Gemini opcode details out of `apollo::ir` until M5 binding layer.
- When IR docs outgrow this page, split to `docs/ir.md` and link it from here.
