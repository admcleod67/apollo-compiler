← [Project milestones index](../milestones.md)

## Milestone 2 — Parser

This document defines the **Milestone 2** Pascal parser for Apollo: a recursive-descent
parser that consumes the Milestone 1 token stream, builds an AST, performs lightweight
name introduction into a basic symbol table, and reports syntax errors with recovery.

It complements:

- [Project milestones](../milestones.md) (Milestone 2 scope)
- [Overview](../overview.md) (pipeline and architectural principles)
- [Milestone 1 — Source & scanner](01-source-and-scanner-infrastructure.md) (token stream to consume)

### Goals

- Consume the immutable `TokenStream` from Milestone 1 — **no rescanning** of source text.
- Ship a recursive-descent parser with an explicit cursor (`current` / `peek` / `advance`).
- Define a Pascal AST sufficient for the **v1 language subset** (see below).
- Introduce a **basic symbol table** for syntactic scopes and name introduction; leave
  full type checking and use-resolution to [Milestone 3](03-semantic-analysis.md).
- Report syntax errors through the existing `DiagnosticEngine`; recover enough to keep
  parsing useful on typical student/example programs.
- Leave IR, codegen, and Gemini out of this milestone.

### Language scope for Milestone 2 (v1 Pascal subset)

Inspired by Mak-style staging: get a working front-end for programs that use **console
I/O**, and defer **Pascal file types / file I/O** to a later milestone.

**In language scope (v1)**

- `program` heading, block structure (`const` / `type` / `var` / subprograms / `begin`…`end`).
- Expressions with classic Pascal operator precedence.
- Statements: compound, assignment, `if`/`while`/`repeat`/`for`, procedure calls.
- Procedures and functions (nested scopes as the grammar requires).
- **Console I/O** via predefined callables treated as ordinary *calls* in the grammar:
  `write`, `writeln`, `read`, `readln` (and optionally `eof` / `eoln` on standard input
  later). The scanner already emits these as identifiers; the parser does not need
  special tokens. Predeclaration / “built-in” recording may land late in M2 or early in
  M3 — what matters for M2 is that call syntax works so `examples/hello.pas` parses.

**Explicitly deferred (not v1)**

- Pascal `file` types (`file of …`), file variables, `reset` / `rewrite` / `close` on
  user files, and `read`/`write` with a file parameter.
- Units, `{$I}` includes, objects/classes, and Turbo-only language features unless
  deliberately adopted later.
- Full ISO 7185 fidelity; expand the subset only when a stage needs it.

`file` remains a **keyword** at the lexer (already true). If it appears in a type
position in v1, the parser should diagnose clearly (“file types not supported yet”)
rather than silently treating it as an identifier.

### Milestone slices (summary)

| Slice | Focus |
|-------|--------|
| **M2a** | Parser framework + program / block skeleton AST |
| **M2b** | Expressions + assignment / simple statements |
| **M2c** | Declarations, control flow, calls (incl. console I/O shape) |
| **M2d** | Basic symbol table, recovery polish, `--ast`, M2 close-out |

Detailed staged delivery is below. Slices map 1:1 to Stages 1–4.

**Method note:** Mak builds comprehension with a working parse that thickens grammar
coverage. Apollo mirrors that with original code: skeleton first, then expressions,
then declarations/control/calls, then symbol-table and driver polish.

---

## Staged delivery plan

Work lands in four mergeable stages. Each stage should leave `main` green (`cmake` build
+ `ctest`) and update the **Implementation status** section when closed.

### Stage 1 — Parser framework & program skeleton (M2a)

**Objective:** Stand up recursive descent over the M1 token stream and parse the smallest
useful compilation unit into an AST: `program Name; begin end.` (optional empty
declaration parts as stubs that accept “nothing”).

**Deliverables**

- Parser types under `include/apollo/pascal/` and implementation under
  `src/pascal/parser/` (names flexible; stay inside `apollo-pascal`).
- Token cursor: index or wrapper over `TokenStream` with `peek`, `advance`, `expect`,
  and `at` / `check` helpers. Do not mutate the stream storage.
- Minimal AST nodes for: compilation unit / `program`, block, compound statement
  (empty), identifier references as needed for the program name.
- `parse(SourceFile&, TokenStream&, DiagnosticEngine&) -> …` entry (ownership of AST
  TBD: unique_ptr tree, arena, or similar — prefer a simple owned tree for M2).
- Unit tests: well-formed tiny program; missing `.` / missing `program` produce
  diagnostics and do not crash.
- Keep `--list` / `--tokens` behaviour unchanged.

**Out of scope for Stage 1**

- Full expression grammar.
- `var` / `const` / `type` / subprogram bodies beyond empty stubs.
- `apolloc --ast` (may wait until Stage 4).
- Symbol table.
- Pascal file I/O syntax.

**Acceptance criteria**

- [x] `program Hello; begin end.` parses to a stable AST shape (program name + empty block).
- [x] Parser never rescans source text; locations come from tokens.
- [x] Syntax errors report via `DiagnosticEngine` (`path:line:col: …`).
- [x] `apollo-pascal` tests remain free of Gemini.

**Status:** completed.

### Stage 2 — Expressions & simple statements (M2b)

**Objective:** Add expression parsing with correct precedence and the first executable
statement forms so assignment and simple calls can appear inside `begin`…`end`.

**Deliverables**

- Expression AST: binary / unary ops, literals (integer, real, string, char),
  identifiers, parenthesized expressions. Optional early support for array indexing /
  field selection only if cheap; otherwise defer until Stage 3 if unused by fixtures.
- Operator precedence aligned with classic Pascal (`not`, multiplicative, additive,
  relational; `and`/`or` as in the accepted subset). Document the table in this file
  when implemented.
- Statements: assignment (`:=`), procedure/function **call form** (name + optional
  argument list), nested compound statements.
- Tests: precedence fixtures (e.g. `1+2*3`); assignment; `writeln('x')`-shaped call
  (identifier + args — no builtin semantics yet).

**Out of scope for Stage 2**

- `if` / `while` / `for` / `repeat` (Stage 3).
- Declaration sections beyond what Stage 1 stubbed.
- Type checking of call arguments (Milestone 3).

**Acceptance criteria**

- [x] Expressions respect the documented precedence table.
- [x] Assignment and call statements populate the compound statement list.
- [x] Malformed expressions emit diagnostics and recover at statement boundaries when
      cheap (e.g. sync on `;` / `end`).

**Status:** completed.

### Expression precedence (Stage 2)

Classic Pascal factor / term / simple-expression / expression layering:

| Level | Operators | Associativity |
|-------|-----------|---------------|
| Primary | literals, identifier, `(expr)`, call | — |
| Unary | `not`, unary `+` `-` | right |
| Multiplicative | `*` `/` `div` `mod` `and` | left |
| Additive | `+` `-` `or` | left |
| Relational | `=` `<>` `<` `<=` `>` `>=` | one optional op (non-chaining) |

Example: `1+2*3` parses as `+(1, *(2,3))`. Example: `not a and b` parses as `(not a) and b`.

`examples/hello.pas` (`writeln(...)` call) parses successfully at Stage 2.

### Stage 3 — Declarations, control flow & calls (M2c)

**Objective:** Cover the rest of the v1 statement/declaration surface so realistic
console programs parse: locals, control structures, and nested subprograms.

**Deliverables**

- Declaration parts: `const`, `type` (subset: aliases, arrays, records as needed —
  expand only as fixtures demand), `var`.
- Statements: `if`/`then`/`else`, `while`/`do`, `repeat`/`until`, `for`/`to`|`downto`/`do`.
- Procedure and function declarations (parameters, local block, return-type for
  functions at the syntactic level).
- Call statements / call expressions sufficient for console I/O shape and user
  subprograms.
- Clear diagnostic when `file` appears in a type denoter (unsupported in v1).
- Tests for each major construct; at least one multi-construct fixture resembling
  `examples/hello.pas` plus a small control-flow example.
- Optional: expand `examples/` with a second `.pas` that uses `if`/`while` and
  `writeln`.

**Design decisions for Stage 3 (defaults unless revisited)**

| Topic | Default for M2 |
|-------|----------------|
| Console I/O | Ordinary calls; names resolved as predefined in late M2 or M3 |
| Pascal file I/O / `file of` | **Out of scope**; diagnose if used |
| `case` / `with` / `goto` / labels | Defer unless a fixture needs them |
| Sets / pointers / packed | Defer; add only if required by a chosen subset |
| Nested functions/procedures | In scope if the grammar already requires nested blocks |
| `forward` declarations | Defer unless needed |

**Acceptance criteria**

- [ ] `examples/hello.pas` parses successfully (call to `writeln` present in AST).
- [ ] Control-flow and declaration fixtures parse; ASTs include the expected node kinds.
- [ ] Unsupported `file` type usage diagnoses without crashing.
- [ ] Still no Gemini dependency.

**Status:** not started.

### Stage 4 — Symbol table, recovery polish & Milestone 2 close-out (M2d)

**Objective:** Add a basic symbol table for syntactic scopes, harden error recovery, and
expose an AST dump from the driver so M2 is demonstrable end-to-end.

**Deliverables**

- Basic symbol table: scope stack; introduce names at declaration sites (program,
  constants, types, variables, parameters, subprograms). Detect **duplicate declaration
  in the same scope**. Recording of predefined console I/O names is encouraged here so
  later semantic analysis has an anchor — full overload/`var` parameter checking stays
  in Milestone 3.
- Use-sites may remain unresolved or lightly recorded; **type checking is Milestone 3**.
- Syntax error recovery: synchronize at statement / declaration boundaries (`;`, `end`,
  `begin`, section keywords) so multiple errors can be reported in one run.
- `apolloc --ast <file>` (or equivalent): parse and print a readable AST dump to stdout;
  diagnostics on stderr; non-zero exit on error-severity diagnostics (same policy as
  `--tokens`).
- Tests: duplicate `var` in one scope; recovery fixture with two syntax errors; CLI
  smoke for `--ast` on `examples/hello.pas`.
- README documents `--ast`.
- Update this document’s **Implementation status** and definition of done.

**Suggested AST dump shape (illustrative)**

```text
Program Hello
  Block
    CompoundStmt
      CallStmt writeln
        Arg StringLiteral 'Hello, Gemini!'
```

Exact formatting is an implementation choice; keep it stable enough for tests.

**Acceptance criteria**

- [ ] Duplicate declarations in one scope diagnose.
- [ ] `--ast` on a clean example exits 0 and prints a non-empty dump.
- [ ] `--ast` on a syntax-error fixture exits non-zero and prints diagnostics.
- [ ] README documents `--list`, `--tokens`, and `--ast`.
- [ ] All Stage 1–4 tests pass under `ctest`.

**Status:** not started.

### Suggested staging cadence

1. **Stage 1** — cursor + program/block skeleton AST.
2. **Stage 2** — expressions + assignment/calls.
3. **Stage 3** — declarations + control flow + subprograms (console I/O as calls).
4. **Stage 4** — basic symbol table, recovery, `--ast`, close-out.

Do not start Milestone 3 semantic work on `main` until Stage 3’s AST shape is stable
enough to type-check (Stage 4 may still be in flight if the AST API is already frozen).

---

## Initial Pascal syntactic subset

The parser should accept at least the following for M2 (expand within a stage when a
fixture needs more). Lexical tokens are already provided by Milestone 1.

**Program / block**

```text
program <id> ; <block> .
block = [const] [type] [var] {subprogram} compound-statement
```

**Statements (v1 minimum)**

- compound (`begin` … `end`)
- assignment
- procedure call (including console I/O names)
- `if` … `then` … [`else` …]
- `while` … `do` …
- `repeat` … `until` …
- `for` … `:=` … `to`|`downto` … `do` …

**Deferred statements / types:** `case`, `with`, `goto`, `file of`, set constructors
as first-class syntax — add only with an explicit stage note.

**Expressions**

- literals and identifiers
- unary `+` `-` `not`
- binary arithmetic / relational / `and` / `or` / `div` / `mod`
- parentheses
- calls in expression position (functions)

Document the concrete precedence table in-tree when Stage 2 lands (section below or a
short `docs/pascal-language.md` if it grows).

---

## Scope and non-goals

### In scope (M2)

- Recursive-descent Pascal parser over the M1 token stream.
- AST for the v1 syntactic subset.
- Basic symbol table (scopes + name introduction + same-scope duplicates).
- Syntax diagnostics and practical recovery.
- `apolloc --ast` at close-out.
- Console I/O **call shape** so hello-world programs parse.

### Deferred (not in this milestone)

- Full semantic analysis / type checking (Milestone 3).
- Shared IR and Gemini codegen (Milestones 4–5).
- Pascal file types and file I/O.
- Include files / units / multi-file programs.
- AST pretty-printer beyond a simple dump, IDE/LSP, or incremental reparsing.

---

## Data model (target shapes)

These are design targets, not frozen APIs. Names may adjust during implementation.

### Parser cursor

- Indexes into an existing `TokenStream`
- `peek(k)`, `advance()`, `expect(TokenKind)`, optional `match`
- Holding `DiagnosticEngine*` / reference for reporting

### AST

- Owned tree (or arena) rooted at a compilation unit / program node
- Every node carries a `SourceRange` (or primary location) from tokens
- Statement and expression hierarchies as sums of node kinds (enum + structs, or
  lightweight variants — prefer clear kinds over a deep OO class tree unless a stage
  shows a benefit)
- Calls are generic; builtin vs user distinction is a symbol/semantic concern

### Symbol table (basic)

- Scope stack tied to program / block / subprogram
- Entries: name (folded or raw + fold key), kind (const/type/var/param/subprogram/…),
  declaration location, optional AST back-pointer
- Predefined console I/O names optional in Stage 4
- No requirement to store types yet (Milestone 3)

---

## Component layout

| Area | Location |
|------|----------|
| Shared source / diagnostics | `include/apollo/common/`, `src/common/` (unchanged) |
| Pascal scanner / tokens | existing `scanner/` (M1) |
| Pascal parser / AST / symbols | `include/apollo/pascal/`, `src/pascal/parser/` (and related) |
| Pascal CMake target | `apollo-pascal` / `apollo::pascal` (extend) |
| Driver | `src/tools/apolloc.cpp` (`--ast` in Stage 4) |
| Tests | `tests/` (parser, AST, symbols, optional CLI) |
| Examples | `examples/*.pas` |

---

## Test matrix

| Stage | Minimum coverage |
|-------|------------------|
| 1 | Tiny `program`…`begin end.`; missing terminator diagnostics |
| 2 | Precedence; assignment; `writeln(...)`-shaped call |
| 3 | `const`/`var`; `if`/`while`/`for`/`repeat`; nested procedure; `file` rejected |
| 4 | Duplicate binding; multi-error recovery; `apolloc --ast` exit 0 / non-zero |

---

## Boundary with Milestone 3

| Concern | Milestone 2 | Milestone 3 |
|---------|-------------|-------------|
| Grammar / AST shape | Yes | Consumes |
| Scope entry / duplicates | Yes (basic) | Extends |
| Type of expressions | No | Yes |
| Resolve identifier uses | Optional / light | Yes |
| `writeln` overloads / types | Call syntax only | Full checking |
| File types / file I/O | Diagnose unsupported | Or later milestone |

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Parser framework & program skeleton | completed |
| Stage 2 — Expressions & simple statements | completed |
| Stage 3 — Declarations, control flow & calls | not started |
| Stage 4 — Symbol table, `--ast`, close-out | not started |

---

## Definition of done (Milestone 2)

- [ ] Stages 1–4 acceptance criteria checked off.
- [ ] `ctest` green on a clean configure/build.
- [ ] README documents `apolloc --ast`.
- [ ] `examples/hello.pas` parses under `--ast`.
- [ ] This status table marked completed.
- [ ] Follow-on version / tag policy decided when closing (may stay `0.1.x` or bump —
      record the choice here when tagging).

---

## Migration / follow-on notes

- Milestone 3 type-checks the Stage 3+ AST; avoid renaming node kinds casually after
  Stage 3 freezes.
- Console I/O builtins should become ordinary symbol-table entries before codegen.
- Pascal file I/O can be a dedicated later slice: type denoters + runtime library +
  codegen — not a parser redesign.
- Prefer AST node *kinds* (enum/struct) consistent with the token design unless a clear
  benefit appears for a class hierarchy.
