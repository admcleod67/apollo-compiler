← [Project milestones index](../milestones.md)

## Milestone 1 — Source & scanner infrastructure (current)

This document defines the **Milestone 1** source and scanner infrastructure for Apollo:
a shared source abstraction and diagnostic subsystem, a Pascal scanner and token stream,
and a program listing utility.

It complements:

- [Project milestones](../milestones.md) (Milestone 1 scope)
- [Overview](../overview.md) (pipeline and architectural principles)

### Purpose

Milestone 1 establishes the first usable compiler substrate. After M1, Apollo can load
Pascal source, tokenize it, report diagnostics with locations, and produce a Mak-style
program listing — without yet parsing into an AST.

This is the minimum structural step needed before the recursive-descent parser (Milestone 2).

**Release note:** Completing M1 is the intended checkpoint for the first tagged release
(`v0.1.0`). Until then, the in-tree version remains `0.1.0-dev`.

### Milestone slices (delivery order)

- **M1a — Source + diagnostics:** Source buffer, source locations, and a diagnostic
  collector shared by later phases.
- **M1b — Pascal tokens + scanner:** Token kinds, token stream, and a Pascal scanner
  (identifiers, literals, operators, comments, whitespace).
- **M1c — Listing + driver wiring:** Program listing utility and `apolloc` options to
  list / dump tokens for a source file.

### M1a — Source + diagnostics

#### Source abstraction

Introduce a host-agnostic source model under `apollo-common` (headers under
`include/apollo/common/`, implementation under `src/common/`):

- **`SourceFile`** (name TBD if a thinner type is preferred): owns or views the full
  source text for one compilation unit, plus a display path/name for diagnostics.
- **`SourceLocation`:** 1-based line and column (and optionally a byte offset) into a
  known source.
- **`SourceRange`:** start/end locations for tokens and later AST nodes.

Requirements:

- Load from a filesystem path and from an in-memory string (tests).
- Line/column mapping must be deterministic for LF and CRLF inputs.
- No dependency on Gemini or Pick filesystem APIs.

#### Diagnostic subsystem

- Severity levels at least: **error**, **warning**, **note** (info optional).
- Each diagnostic carries: severity, message, primary location (and optional related
  ranges later).
- A **`DiagnosticEngine`** (or equivalent collector) accumulates diagnostics for a
  compile/list session and can render them to a stream in a stable format, e.g.:

  `path:line:col: error: message`

- Scanner (and later parser) report through this API; they do not print ad hoc strings.

### M1b — Pascal tokens + scanner

#### Token model

- **`TokenKind`:** enumerates Pascal lexical categories needed for a useful first
  subset (keywords, identifiers, integer/real/string/char literals, punctuation,
  operators, end-of-file, and an invalid/error kind if useful).
- **`Token`:** kind, lexeme (or sourced span), and `SourceRange`.
- **`TokenStream`:** ordered sequence of tokens produced by one scan of a source file
  (random-access or cursor-based is an implementation choice; the parser will consume
  it in Milestone 2).

Exact Pascal dialect (ISO 7185 subset vs Turbo-style extensions) need not be finalized
in M1, but the scanner should document which keywords and literal forms it accepts and
reject or flag unknowns consistently.

#### Scanner responsibilities

- Skip whitespace and comments (`{ ... }`, `(* ... *)`; decide whether `//` is in or
  out of scope and document it).
- Recognize identifiers and keywords (case-insensitive per classic Pascal, unless a
  deliberate dialect choice says otherwise — document the choice).
- Recognize numeric and string/character literals with clear error recovery on
  malformed literals (emit a diagnostic, produce an error token or skip to a sync
  point).
- Never throw for ordinary lexical errors; prefer diagnostics + continued scanning
  where recovery is cheap.

Implementation lives under `src/pascal/scanner/` with public headers under
`include/apollo/pascal/` (or `include/apollo/pascal/scanner/` if subdivided).

### M1c — Listing + driver wiring

#### Program listing utility

A listing tool (library API used by `apolloc`, and optionally a thin dedicated binary
later) prints a numbered source listing suitable for teaching/debugging workflows:

- Line numbers aligned with `SourceLocation` line numbers.
- Optional annotation of the current token stream (e.g. dump tokens after the listing,
  or interleave — pick one and document it).

This follows the conceptual role of a source listing in Mak-style compiler front ends;
the implementation is original Apollo code.

#### `apolloc` surface (minimum)

Extend the driver beyond `--version` / `--help`, for example:

- `apolloc --list <file>` — print numbered listing
- `apolloc --tokens <file>` — scan and dump token stream
- Exit non-zero if errors were reported during scan

Exact flag names may vary; keep them stable once merged and document them in the
README or a short tools note.

### Scope and non-goals

#### In scope (M1)

- Shared source buffer / location / range types.
- Diagnostic collection and stable console rendering.
- Pascal scanner + token stream for an initial lexical subset.
- Program listing and driver options to exercise source + scanner.
- Unit tests for locations, diagnostics, and scanner cases.
- Documentation of accepted Pascal lexical rules for this milestone.

#### Deferred (not in this document's implementation scope)

- Recursive-descent parser and AST (Milestone 2).
- Symbol tables beyond what the scanner needs (none).
- Semantic analysis, IR, and codegen (Milestones 3–5).
- Full ISO/Turbo Pascal fidelity (expand incrementally with later milestones).
- Linking against Gemini PickVM or emitting `.tbc` (still prefer text `.tbc` later;
  not required for M1).
- Preprocessor / include-file resolution beyond a single source file (unless a
  minimal include proves necessary — default is single-file units).

### Data model (target shapes)

These are design targets, not frozen APIs. Names may adjust during implementation.

#### `SourceFile`

- `path` / display name
- full text (`std::string` or string view over owned storage)
- helpers: `lineColumnAt(offset)`, `lineText(line)`

#### `SourceLocation` / `SourceRange`

- 1-based line and column for human diagnostics
- optional absolute offset for efficient scanner use

#### `Diagnostic`

- severity, message, primary location
- optional source snippet attachment later (stretch)

#### `Token` / `TokenKind` / `TokenStream`

- tokens reference ranges into the owning `SourceFile`
- EOF token terminates the stream

### Component layout

| Area | Location |
|------|----------|
| Source + diagnostics | `include/apollo/common/`, `src/common/` |
| Pascal scanner | `include/apollo/pascal/`, `src/pascal/scanner/` |
| Listing helper | `src/common/` or `src/tools/` (library preferred) |
| Driver | `src/tools/apolloc.cpp` |
| Tests | `tests/` (scanner, locations, diagnostics, listing) |
| Examples | `examples/` (small `.pas` files for manual runs) |

### Test matrix

Minimum expected tests for this milestone:

- Source load from string and path; LF / CRLF line mapping.
- Diagnostic formatting includes path, line, and column.
- Scanner: keywords vs identifiers; integer/real/string/char literals; operators;
  `{ }` and `(* *)` comments.
- Scanner recovery: unclosed string / comment produces diagnostics and does not crash.
- Listing: line numbers match `SourceLocation` lines for a fixture file.
- `apolloc --tokens` (or equivalent) exits non-zero when lexical errors exist.

### Implementation status

**Not started.** Milestone 0 provides the repository skeleton, `apollo-common` version
stub, and `apolloc` `--version` / `--help` only.

As slices land, update this section (M1a / M1b / M1c) in the same way Gemini records
delivery status on active milestone docs.

### Migration / follow-on notes

- Keep lexical rules documented here or in a dedicated `pascal-language.md` once the
  subset is large enough to deserve its own page.
- Parser (M2) should consume the M1 token stream without rescanning.
- Do not special-case Gemini paths in `common`; host I/O stays ordinary filesystem /
  memory buffers in M1.
