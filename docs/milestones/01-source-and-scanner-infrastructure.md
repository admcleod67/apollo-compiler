← [Project milestones index](../milestones.md)

## Milestone 1 — Source & scanner infrastructure (completed)

This document defines the **Milestone 1** source and scanner infrastructure for Apollo:
a shared source abstraction and diagnostic subsystem, a Pascal scanner and token stream,
and a program listing utility.

It complements:

- [Project milestones](../milestones.md) (Milestone 1 scope)
- [Overview](../overview.md) (pipeline and architectural principles)

### Goals

- Follow the Mak-style build-up: the **first executable capability** is a source
  **listing utility**, then thicken shared infrastructure, then add the scanner.
- Establish a usable compiler substrate: load Pascal source, print numbered listings,
  tokenize with diagnostics, and dump tokens — without yet parsing into an AST.
- Keep shared infrastructure in `apollo-common` so later front-ends reuse source and
  diagnostic types without depending on Pascal.
- Leave the parser (Milestone 2) a clean consumer of an immutable token stream — no
  rescanning of source text.

**Release note:** Milestone 1 is complete and ready for the first tagged release
(`v0.1.0`). Until that tag is cut, the toolchain still reports `0.1.0-dev`
(`${PROJECT_VERSION}-dev` from CMake). Drop the `-dev` suffix when tagging.

### Milestone slices (summary)

| Slice | Focus |
|-------|--------|
| **M1a** | Source buffer + **first listing executable** (`apolloc --list`) |
| **M1b** | Locations/ranges + diagnostic engine (on the same source model) |
| **M1c** | Pascal `TokenKind` / `Token` / scanner / token stream |
| **M1d** | Improved listing + `--tokens`, examples, M1 close-out |

Detailed staged delivery is below. Slices map 1:1 to Stages 1–4.

**Method note:** In *Writing Compilers and Interpreters*, the first executable is the
listing utility. Apollo mirrors that ordering with original code: a simple list-first
cut, then an improved listing once the scanner exists.

---

## Staged delivery plan

Work lands in four mergeable stages. Each stage should leave `main` green (`cmake` build
+ `ctest`) and update the **Implementation status** section when closed.

### Stage 1 — Source buffer & listing utility (M1a) — first executable

**Objective:** Ship the first useful `apolloc` behaviour: load a source file and print a
numbered listing. This is the Milestone 1 “hello world” of the toolchain.

**Deliverables**

- Minimal `SourceFile` (or equivalent) in `apollo-common`: display path + full text;
  construct from filesystem path and from in-memory string (for tests).
- Line splitting that treats LF and CRLF as a single logical newline (so listing line
  numbers stay stable across hosts).
- Language-agnostic listing helper (library API preferred): print `NNNN: <line text>`
  (exact width/padding is an implementation choice; keep it stable for tests).
- `apolloc --list <file>` wired to that helper; retain `--version` / `--help`.
- At least one example under `examples/` (e.g. a tiny `.pas` stub) suitable for
  `--list`.
- Unit/CLI tests: listing line count and numbering for a fixture; missing file fails
  cleanly.
- README documents `--list`.

**Out of scope for Stage 1**

- Full `SourceLocation` / column mapping (may be stubbed or deferred to Stage 2).
- `DiagnosticEngine`.
- Scanner / tokens.
- `--tokens`.

**Acceptance criteria**

- [x] `apolloc --list examples/...` prints every source line with 1-based numbers.
- [x] In-memory and path-loaded text produce the same listing for the same contents.
- [x] CRLF fixtures list with the same line count as LF equivalents.
- [x] No Gemini / Pick filesystem dependency.

**Status:** completed.

### Stage 2 — Locations & diagnostics (M1b)

**Objective:** Thicken the Stage 1 source model so later phases (scanner, improved
listing, parser) share one location and diagnostic story. **Do not invent a second line
numbering scheme** — listing and locations must agree.

**Deliverables**

- `SourceLocation` / `SourceRange`: 1-based line/column; optional byte offset for
  scanner internals.
- Helpers such as `lineColumnAt(offset)` and `lineText(line)` on the existing
  `SourceFile`.
- `Diagnostic` + severity (`error`, `warning`, `note`).
- `DiagnosticEngine` (collector): append diagnostics; query error count; render to a
  stream in a stable format:

  `path:line:col: severity: message`

- Unit tests for mapping and diagnostic formatting.
- Optionally re-check that `--list` line numbers still match `SourceLocation` lines
  after the refactor.

**Acceptance criteria**

- [x] In-memory and path-loaded sources agree on line/column for the same text.
- [x] CRLF fixtures map to the same line numbers as LF-normalized equivalents.
- [x] Diagnostic rendering always includes path, line, column, severity, and message.
- [x] Stage 1 `--list` still works and uses the same line numbers as locations.

**Status:** completed.

### Stage 3 — Pascal tokens & scanner (M1c)

**Objective:** Lex a single Pascal compilation unit into a complete token stream with
diagnostics on lexical errors.

**Deliverables**

- Public Pascal headers under `include/apollo/pascal/` (scanner types at minimum).
- Implementation under `src/pascal/scanner/`.
- CMake target `apollo-pascal` (static lib) depending on `apollo::common`.
- `TokenKind`, `Token` (kind + range + lexeme or span into `SourceFile`), `TokenStream`.
- `Scanner` / `scan(SourceFile&, DiagnosticEngine&) -> TokenStream` API (names flexible).
- Keyword table for the initial subset (see **Initial Pascal lexical subset**).
- Comment handling: `{ ... }` and `(* ... *)` (nested comments out of scope).
- Whitespace skipping; identifiers vs keywords; integer / real / string / char literals;
  punctuation and multi-character operators (`:=`, `<>`, `<=`, `>=`, `..`).
- Lexical error recovery: diagnose and continue where cheap (unclosed string/comment,
  bad numeric forms); do not abort the process.
- Unit tests covering happy path and recovery cases.
- Short in-tree note of accepted lexical rules (section in this file is enough for M1;
  split to `docs/pascal-language.md` only if it grows unwieldy).

**Design decisions for Stage 3 (defaults unless revisited)**

| Topic | Default for M1 |
|-------|----------------|
| Identifier / keyword case | Case-insensitive matching; preserve original lexeme spelling in the token |
| `//` line comments | **Out of scope** (not classic Pascal); `/` is always `Slash` |
| Nested `{` / `(*` comments | **Out of scope**; first closer wins; unclosed → error at opener |
| String quotes | Single-quoted Pascal strings; `''` as embedded quote |
| Character literals | Single-quoted form whose **decoded** content length is 1 → `CharLiteral`; otherwise `StringLiteral` |
| Real literals | Digits with `.` + digit, and/or `E`/`e` exponent; `1..2` is integer + `DotDot` + integer |
| Hex / binary literals | **Out of scope** unless needed for a tiny fixture — defer |
| Dollar / compiler directives | **Out of scope** |

**Acceptance criteria**

- [x] Scanning a small well-formed `.pas` fixture yields a stable, ordered token list ending in EOF.
- [x] Keywords are distinguished from identifiers under case folding (`Begin` → keyword).
- [x] `{ }` and `(* *)` comments produce no tokens.
- [x] Malformed string / unclosed comment emit diagnostics and still return a stream (no crash).
- [x] `apollo-pascal` links in tests without pulling Gemini.

**Status:** completed.

### Stage 4 — Improved listing, token dump & Milestone 1 close-out (M1d)

**Objective:** Upgrade the Stage 1 listing/driver surface now that diagnostics and the
scanner exist, then close M1 for `v0.1.0`.

**Deliverables**

- `apolloc --tokens <file>` — load, scan, dump tokens (one token per line; include kind
  + location; include lexeme when useful).
- Exit status policy: non-zero if any **error**-severity diagnostics were produced
  (warnings do not fail the process unless documented otherwise). Apply consistently to
  `--tokens` (and to `--list` only if listing itself can report errors, e.g. I/O).
- Optional listing improvements (pick what is worth doing before tag):

  - ensure listing line numbers are proven identical to scanner `SourceLocation` lines
  - optional header/footer (path, line count) — stretch
  - do **not** require interleaving tokens into the listing for M1

- Tests for `--tokens` exit codes on clean vs erroneous input; listing/location parity
  regression if not already covered.
- README documents `--list` and `--tokens`.
- Update this document’s **Implementation status** to closed; bump version string from
  `0.1.0-dev` to `0.1.0` when tagging (tagging itself is a release step, not a code stage).

**Token dump format (suggested)**

```text
1:1-1:7    KeywordProgram    program
1:8-1:8    Identifier        Hello
...
N:M-N:M    EndOfFile
```

Stable enough for golden tests; refine if needed before tag.

**Acceptance criteria**

- [x] `apolloc --list` still works (Stage 1 behaviour preserved or intentionally improved).
- [x] `apolloc --tokens` on a clean file exits 0 and dumps EOF-terminated tokens.
- [x] `apolloc --tokens` on a fixture with a lexical error exits non-zero and prints
      diagnostics to stderr (or a documented stream).
- [x] README documents both flags.
- [x] All Stage 1–4 tests pass under `ctest`.

**Status:** completed.

### Suggested staging cadence

1. **Stage 1** — source buffer + `--list` (first executable, Mak-style).
2. **Stage 2** — locations + diagnostics on that same source model.
3. **Stage 3** — `apollo-pascal` scanner + tests (driver token dump not required yet).
4. **Stage 4** — `--tokens`, exit-on-error, polish, tag `v0.1.0`.

Do not start Milestone 2 parser work on `main` until Stage 3’s token stream API is
stable enough to consume (Stage 4 may still be in flight if the API is already frozen).

---

## Initial Pascal lexical subset

The scanner should recognize at least the following for M1 (expand later as the parser
needs more):

**Keywords (illustrative minimum):**  
`and`, `array`, `begin`, `case`, `const`, `div`, `do`, `downto`, `else`, `end`,
`file`, `for`, `function`, `goto`, `if`, `in`, `label`, `mod`, `nil`, `not`, `of`,
`or`, `packed`, `procedure`, `program`, `record`, `repeat`, `set`, `then`, `to`,
`type`, `until`, `var`, `while`, `with`.

Add `xor` / Turbo extensions only if deliberately chosen; default is classic-leaning
subset. Document any extras in this file when added.

**Punctuation / operators:**  
`+` `-` `*` `/` `=` `<` `>` `[` `]` `.` `,` `:` `;` `(` `)` `^` `@`  
`:=` `<>` `<=` `>=` `..`

**Literals:** integer, real, string, character (as decided in Stage 3 table).

---

## Scope and non-goals

### In scope (M1)

- Source buffer and the first listing executable (`apolloc --list`).
- Shared location / range types and diagnostic collection.
- Pascal scanner + token stream for the initial lexical subset.
- Improved driver surface (`--tokens`, exit-on-error) at close-out.
- Unit and smoke tests for Stages 1–4.
- Documentation of accepted Pascal lexical rules for this milestone.

### Deferred (not in this milestone)

- Recursive-descent parser and AST (Milestone 2).
- Symbol tables (Milestone 2+).
- Semantic analysis, IR, and codegen (Milestones 3–5).
- Full ISO 7185 / Turbo Pascal fidelity.
- Include/`{$I}` / multi-file units.
- Linking against Gemini PickVM or emitting `.tbc`.
- Pretty-printed diagnostic snippets / carets (stretch; plain `path:line:col` is enough).
- Token-annotated / interleaved source listings (optional stretch only).

---

## Data model (target shapes)

These are design targets, not frozen APIs. Names may adjust during implementation.

### `SourceFile`

- `path` / display name
- full text (`std::string` or string view over owned storage)
- Stage 1: enough to list lines
- Stage 2+: helpers `lineColumnAt(offset)`, `lineText(line)`

### `SourceLocation` / `SourceRange`

- Introduced in Stage 2
- 1-based line and column for human diagnostics
- optional absolute offset for efficient scanner use

### `Diagnostic`

- Introduced in Stage 2
- severity, message, primary location
- related ranges deferred

### `Token` / `TokenKind` / `TokenStream`

- Introduced in Stage 3
- tokens reference ranges into the owning `SourceFile`
- EOF token terminates the stream
- stream is suitable for lookahead in Milestone 2 (index or cursor with peek)

---

## Component layout

| Area | Location |
|------|----------|
| Source (+ later diagnostics) | `include/apollo/common/`, `src/common/` |
| Listing helper | `src/common/` preferred (language-agnostic); used from Stage 1 |
| Pascal scanner | `include/apollo/pascal/`, `src/pascal/scanner/` (Stage 3) |
| Pascal CMake target | `src/pascal/` → `apollo-pascal` / `apollo::pascal` |
| Driver | `src/tools/apolloc.cpp` (`--list` in Stage 1; `--tokens` in Stage 4) |
| Tests | `tests/` (listing, source, diagnostics, scanner; optional CLI tests) |
| Examples | `examples/*.pas` (from Stage 1) |

---

## Test matrix

| Stage | Minimum coverage |
|-------|------------------|
| 1 | Path/string load; LF vs CRLF listing; `apolloc --list` smoke |
| 2 | Line/column mapping; diagnostic render shape; list/location parity |
| 3 | Keywords vs identifiers; literals; operators; both comment forms; unclosed string/comment recovery |
| 4 | `--tokens` exit 0 / non-zero; help/README mention both flags |

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Source & listing utility (first executable) | completed |
| Stage 2 — Locations & diagnostics | completed |
| Stage 3 — Pascal scanner | completed |
| Stage 4 — Improved listing, `--tokens`, close-out | completed |

Milestone 1 Stages 1–4 are implemented. Tag `v0.1.0` (and drop `-dev` from the
version string) as a separate release step.

---

## Definition of done (Milestone 1 / `v0.1.0`)

- [x] Stages 1–4 acceptance criteria checked off.
- [x] `ctest` green on a clean configure/build.
- [x] README documents `apolloc --list` and `--tokens`.
- [x] This status table marked completed.
- [ ] Git tag `v0.1.0` cut from that revision (version string without `-dev`) — release follow-up.

---

## Migration / follow-on notes

- Parser (M2) consumes the Stage 3 token stream without rescanning.
- Keep Gemini out of `common` and out of the Pascal scanner; host I/O is ordinary
  filesystem / memory buffers.
- When lexical rules outgrow this page, split to `docs/pascal-language.md` and link it
  from here.
