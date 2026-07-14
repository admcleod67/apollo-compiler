← [Project milestones index](../milestones.md)

## Milestone 3 — Semantic analysis

This document defines the **Milestone 3** Pascal semantic analysis for Apollo: enrich the
Milestone 2 symbol table with types, resolve identifier uses, type-check expressions and
statements (including console I/O builtins), and report semantic errors through the
existing `DiagnosticEngine`.

It complements:

- [Project milestones](../milestones.md) (Milestone 3 scope)
- [Overview](../overview.md) (pipeline and architectural principles)
- [Milestone 2 — Parser](02-parser.md) (AST and basic symbol table to consume)
- [Milestone 4 — Intermediate representation](04-intermediate-representation.md) (next consumer)

### Goals

- Consume the Milestone 2 `Program` AST — **no re-parsing** and no rescanning of source.
- Extend the Stage-4 symbol table so symbols carry **types** (and subprogram signatures).
- Resolve identifier **use-sites** against scopes (undeclared name → error).
- Type-check expressions and statements for the **v1 Pascal subset** already parsed in M2.
- Type-check **console I/O** builtins (`write` / `writeln` / `read` / `readln`) with
  pragmatic overload rules; leave Pascal **file** I/O deferred.
- Keep IR, Gemini codegen, and multi-file units out of this milestone.
- Leave `main` green after each stage (`cmake` build + `ctest`).

### Language / semantic scope (v1)

Aligned with Milestone 2’s syntactic subset:

**In scope**

- Predefined types: at least `integer`, `real`, `boolean`, `char` (and string/char
  literals as used by console I/O). Alias types and `array [lo..hi] of T` from M2
  denoters.
- Const / var / type / param / function / procedure bindings with types.
- Expression typing (unary/binary ops, literals, calls, groups) with classic Pascal
  compatibility rules for the subset.
- Statement checking: assignment, calls, `if`/`while`/`repeat` conditions (boolean),
  `for` control variable and bounds, nested compounds.
- Builtins `write`, `writeln`, `read`, `readln` with argument-list checking suitable for
  `examples/hello.pas` and `examples/count.pas`.

**Explicitly deferred**

- Pascal `file` types and file-parameter I/O.
- Sets, pointers, `packed`, records (unless a fixture forces a minimal record later —
  default is out).
- `case` / `with` / `goto` (not in the M2 grammar).
- Full ISO 7185 / Turbo fidelity; expand only when a stage needs it.
- Shared IR (Milestone 4) and Gemini codegen (Milestone 5).

### Milestone slices (summary)

| Slice | Focus |
|-------|--------|
| **M3a** | Typed symbol table + type denoter resolution + predefined types |
| **M3b** | Use-resolution + expression type checking |
| **M3c** | Statement semantics + console I/O builtins |
| **M3d** | Driver check mode, recovery polish, M3 close-out |

Detailed staged delivery is below. Slices map 1:1 to Stages 1–4.

**Method note:** Mak thickens “understanding” after a working parse. Apollo mirrors that
with original code: first make declarations *mean* types, then type expressions, then
enforce statement rules and builtins, then expose checking from `apolloc`.

---

## Staged delivery plan

Work lands in four mergeable stages. Each stage should leave `main` green and update the
**Implementation status** section when closed.

### Stage 1 — Typed symbols & type resolution (M3a)

**Objective:** Turn the Milestone 2 declare-only table into a typed symbol table:
predefined scalar types, resolve `TypeDenoter`s on decls, and attach types to const/var/
type/param/subprogram entries. Still **no** use-site resolution or expression checking.

**Deliverables**

- Implementation under `src/pascal/semantic/` (headers under `include/apollo/pascal/` as
  needed), linked into `apollo-pascal`.
- A `Type` representation suitable for the v1 subset (kinds: predefined scalars, alias,
  array; optional “error / unknown” type for recovery). Prefer kind + struct over a deep
  class tree unless a clear benefit appears.
- Preseed symbols for predefined types (`integer`, `real`, `boolean`, `char`, …) and keep
  existing console I/O **builtin** symbols (signatures can stay stubbed until Stage 3).
- Extend or replace `buildSymbolTable` so declarations store resolved types; diagnose
  unknown type names and unsupported denoters (e.g. already-rejected `file`).
- Const declarations: check that the initializer is a constant expression of a resolvable
  type (literals / const refs only in Stage 1 if cheap; otherwise type the literal and
  defer full const-expr evaluation).
- Unit tests: alias `type T = integer;` binds; `var i: integer;` has integer type;
  unknown type name diagnoses; array denoter resolves element type.
- Keep `--list` / `--tokens` / `--ast` behaviour unchanged (AST dump may ignore types).

**Out of scope for Stage 1**

- Expression type checking / use-site lookup.
- Assignment and call checking.
- `apolloc --check` (Stage 4).

**Acceptance criteria**

- [x] Predefined types are visible in the outermost scope.
- [x] Successful `var` / `type` / `const` fixtures attach resolved types to symbols.
- [x] Unknown type identifier in a denoter reports a diagnostic (no crash).
- [x] Existing M2 tests remain green.

**Stage 1 notes**

- `Type` is kind + struct (`TypeTag`: Integer/Real/Boolean/Char/String/Alias/Array/Error)
  with `shared_ptr` chaining for Alias/Array; helpers live in `semantic/Type.cpp`.
- Const types come from **literal** initializers only (including a group around a literal);
  other forms get Error + a diagnostic.
- `buildSymbolTable` returns `SymbolTable` by value; denoter name lookup uses
  `SymbolTable::lookup`.

**Status:** completed.

### Stage 2 — Use resolution & expression typing (M3b)

**Objective:** Resolve identifiers in expressions and compute an expression type for every
well-typed node (or mark error types and continue).

**Deliverables**

- Semantic walk (visitor or recursive functions) over expressions:
  - literals → predefined types
  - identifiers → lookup; undeclared → error
  - unary / binary ops → result type with subset compatibility (`integer`/`real`
    arithmetic; `boolean` for `and`/`or`/`not` and relational results as boolean; …)
  - calls in expression position → result type of function (arity/args may be partial
    until Stage 3 if cheaper to finish signatures there)
  - groups → inner type
- Optional: store inferred type on AST nodes, or keep a side-table keyed by node
  address/range — pick one approach and stick to it for M3.
- Tests: undeclared identifier; `1 + 2` is integer; `1 + 2.0` is real (or documented
  promotion); `1 < 2` is boolean; bad operand types diagnose.

**Out of scope for Stage 2**

- Full statement checking (assignment LHS categories, `for` rules, …) — Stage 3.
- Complete `writeln` overload matrix — Stage 3 (calls may accept “unknown” pending).

**Acceptance criteria**

- [x] Undeclared identifiers in expressions diagnose.
- [x] Documented subset of operators produces the expected result types.
- [x] Type errors do not abort the process; analysis continues where cheap.

**Stage 2 notes**

- Inferred types are stored on `ast::Expr::type` (`TypePtr`); statements are not typed.
- Entry point: `analyse(Program&, DiagnosticEngine&) -> SymbolTable` in
  `semantic/Analyse.cpp` (declares while typing bodies before `popScope`).
  `buildSymbolTable` forwards to `analyse`.
- Predefined consts `true` / `false` (Boolean) are seeded in the outermost scope.
- Operator result rules (Stage 2):
  - `+` `-` `*`: both Integer → Integer; numeric mix with Real → Real
  - `/`: numeric → Real
  - `div` / `mod`: both Integer → Integer
  - Unary `+`/`-`: Integer or Real → same; `not`: Boolean → Boolean
  - `and` / `or`: Boolean → Boolean
  - Relationals: numeric pairs or same Char/Boolean/Integer/Real → Boolean
  - If either operand is Error, result is Error with no cascade diag

**Status:** completed.

### Stage 3 — Statements & console I/O (M3c)

**Objective:** Enforce statement-level rules and type-check calls, including console I/O
builtins, so `examples/hello.pas` and `examples/count.pas` are **semantically** clean.

**Deliverables**

- Assignment: LHS must be a variable (or function-name for result assignment, if in
  subset); RHS compatible with LHS type.
- Control: `if` / `while` / `repeat` conditions must be boolean; `for` control var
  ordinal/integer subset rules; bounds compatible.
- Procedure / function calls: arity and argument compatibility against symbol signatures.
- Console builtins:
  - `write` / `writeln`: zero or more args of printable types (integer, real, char,
    string/char-array as decided); optional field-width forms deferred unless cheap.
  - `read` / `readln`: variable arguments of readable types.
  - Document the accepted matrix in this file when implemented.
- Tests for each major rule; golden “clean” runs on hello + count text; intentional
  bad fixtures (assign real to boolean, `if 1 then`, wrong `writeln` arg, …).

**Design decisions for Stage 3 (defaults unless revisited)**

| Topic | Default for M3 |
|-------|----------------|
| String literals | Compatible with `writeln` / `write`; exact stored type is an impl choice |
| `integer` ↔ `real` | Allow widening integer→real in expressions/assignments as documented |
| Nested functions | Resolve/check if present in AST; no new grammar |
| File-parameter I/O | Still out — diagnose if somehow present |

**Acceptance criteria**

- [ ] `examples/hello.pas` and `examples/count.pas` analyse with zero semantic errors.
- [ ] Assignment / condition / `for` / call mismatch fixtures diagnose.
- [ ] Builtin `writeln('…')` and `writeln(i)` both check clean for the examples’ shapes.

**Status:** not started.

### Stage 4 — Driver check mode & Milestone 3 close-out (M3d)

**Objective:** Expose semantic analysis from the driver, harden multi-error reporting, and
close Milestone 3.

**Deliverables**

- `apolloc --check <file>` (or fold checking into `--ast` with a documented flag —
  **prefer a dedicated `--check` / `-c`** so dump and analyse stay separable):
  load → scan → parse → build typed symbols → analyse → diagnostics on stderr;
  non-zero exit on any error-severity diagnostic (same policy as `--tokens` / `--ast`).
- Optionally print a short “OK” / summary on success; no Gemini output.
- Recovery: continue after semantic errors where cheap so multiple issues appear in one
  run (fixture with ≥2 semantic errors).
- README documents `--check`.
- Update this document’s **Implementation status** and definition of done.
- Version / tag policy: bump to **`0.3.0`** when closing (tag `v0.3.0` as a separate
  release follow-up), unless release notes say otherwise — record the choice in the DoD.

**Acceptance criteria**

- [ ] `--check` on `examples/hello.pas` exits 0 with no stderr errors.
- [ ] `--check` on a semantic-error fixture exits non-zero and prints diagnostics.
- [ ] README documents `--list`, `--tokens`, `--ast`, and `--check`.
- [ ] All Stage 1–4 tests pass under `ctest`.

**Status:** not started.

### Suggested staging cadence

1. **Stage 1** — typed symbol table + denoter resolution.
2. **Stage 2** — use-resolution + expression types.
3. **Stage 3** — statements + console I/O checking.
4. **Stage 4** — `--check`, polish, close-out / `0.3.0`.

Do not start Milestone 4 IR work on `main` until Stage 3’s type attachments are stable
enough to lower (Stage 4 may still be in flight if the type API is frozen).

---

## Type system (v1 target)

Design targets, not frozen APIs.

| Kind | Notes |
|------|--------|
| `Integer` / `Real` / `Boolean` / `Char` | Predefined |
| `String` / string-literal type | As needed for `writeln`; may be distinct from `char` |
| `Alias` | Name → underlying type |
| `Array` | Index bounds (static exprs) + element type |
| `Error` | Poison type for recovery |

**Compatibility (Stage 2 expression rules):**

- Arithmetic `+` `-` `*`: numeric operands; Integer if both Integer, else Real.
- `/`: numeric → Real. `div` / `mod`: Integer operands → Integer.
- Relational: compatible operands → `boolean`.
- Boolean ops (`and` / `or` / `not`): `boolean` operands → `boolean`.
- Assignment widening and statement rules remain Stage 3.

---

## Scope and non-goals

### In scope (M3)

- Typed symbol table and type denoter resolution.
- Identifier use-resolution.
- Expression and statement type checking for the M2 v1 subset.
- Console I/O builtin checking.
- `apolloc --check` at close-out.
- Semantic diagnostics via `DiagnosticEngine`.

### Deferred (not in this milestone)

- Shared IR and Gemini codegen (Milestones 4–5).
- Pascal file I/O / `file of`.
- Records, sets, pointers, `packed` (unless a later explicit stage note).
- Optimization, flow-sensitive analysis, definite-assignment beyond simple rules.
- Multi-file units / `{$I}`.

---

## Data model (target shapes)

### Typed `Symbol`

Extend M2 entries with:

- Resolved `Type` (or type id)
- For subprograms / builtins: parameter types + `var` flags + optional return type

Lookup by folded name still case-insensitive.

### Analyser API (illustrative)

```text
analyse(Program&, DiagnosticEngine&) -> void
  // assumes or builds typed symbol table; reports semantic errors
```

May return a small `AnalysisResult` (error count, optional typed AST annotations).

### Driver pipeline

```text
--check:  load → scan → parse → analyse → diagnostics → exit code
--ast:    unchanged dump (may optionally run analyse first — document if so)
```

Default: `--ast` stays dump-oriented; `--check` owns analysis exit policy.

---

## Component layout

| Area | Location |
|------|----------|
| Shared diagnostics / source | `include/apollo/common/`, `src/common/` |
| Pascal AST / parser / M2 symbols | existing |
| Semantic analysis | `include/apollo/pascal/`, `src/pascal/semantic/` |
| Pascal CMake target | `apollo-pascal` (extend) |
| Driver | `src/tools/apolloc.cpp` (`--check` in Stage 4) |
| Tests | `tests/` (semantic unit + CLI) |
| Examples | `examples/hello.pas`, `examples/count.pas` (must check clean) |

---

## Test matrix

| Stage | Minimum coverage |
|-------|------------------|
| 1 | Predefined types; alias/var typing; unknown type name |
| 2 | Undeclared id; arithmetic / relational result types; bad operands |
| 3 | Assignment / boolean conditions / `for`; `writeln` shapes; hello + count clean |
| 4 | `--check` exit 0 / non-zero; multi semantic-error fixture; README |

---

## Boundary with adjacent milestones

| Concern | Milestone 2 | Milestone 3 | Milestone 4 |
|---------|-------------|-------------|-------------|
| AST shape | Owns | Consumes | Consumes |
| Declare / duplicates | Basic | Extends with types | Uses |
| Use-resolution / typing | No | Yes | Uses annotated or re-walks |
| Console I/O meaning | Call syntax | Type-checked builtins | Lowered to IR/runtime |
| File I/O | Rejected | Still deferred | Or later |
| IR / `.tbc` | No | No | Yes |

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Typed symbols & type resolution | completed |
| Stage 2 — Use resolution & expression typing | completed |
| Stage 3 — Statements & console I/O | not started |
| Stage 4 — `--check`, close-out | not started |

---

## Definition of done (Milestone 3)

- [ ] Stages 1–4 acceptance criteria checked off.
- [ ] `ctest` green on a clean configure/build.
- [ ] README documents `apolloc --check`.
- [ ] `examples/hello.pas` and `examples/count.pas` pass `--check`.
- [ ] This status table marked completed.
- [ ] Version policy recorded (default: report `0.3.0`; cut git tag `v0.3.0` as follow-up).

---

## Migration / follow-on notes

- Milestone 4 should lower typed Pascal constructions without inventing a second type
  system — reuse M3 `Type` ideas or map explicitly to IR types.
- Prefer extending the M2 symbol table in place over a parallel shadow table, unless
  staging forces a temporary split.
- Keep Gemini out of `semantic/`; host I/O remains ordinary diagnostics / driver exit
  codes.
- When semantic rules outgrow this page, split to `docs/pascal-language.md` (types /
  checking) and link it from here.
