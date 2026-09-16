# Apollo Compiler

A multi-language **compiler toolchain** targeting the **Gemini Virtual Machine (VM)**.

Apollo is a sister project to [gemini-system](../gemini-system): Gemini hosts the Pick-inspired
environment and bytecode VM; Apollo builds portable language front-ends that compile to that VM.

| | |
|---|---|
| **Initial language** | Pascal (Wirth console / Pascal80-style completeness next) |
| **Later languages** | BASIC, COMAL, Fortran, COBOL (deferred — Milestone 8) |
| **Target** | Gemini VM bytecode (`.tbc` text format) |

## Design philosophy

Architecture and development follow a methodology inspired by Ronald Mak’s
*Writing Compilers and Interpreters*, implemented entirely from original code.

> **Attribution:** Apollo Compiler is an original open-source project whose architecture and
> development approach are inspired by Ronald Mak’s *Writing Compilers and Interpreters*.
> No code or text from the book is used directly; only the conceptual methodology informs the design.

## Status

**Milestone 1 — Source & scanner infrastructure** is complete.
**Milestone 2 — Parser** is complete.
**Milestone 3 — Semantic analysis** is complete (`0.3.0`).
**Milestone 4 — Intermediate representation** is complete (`0.4.0`).
**Milestone 5 — Code generator** is complete (`0.5.0`).
**Milestone 6 — Standalone VM execution** is complete (`0.6.0`).
**Milestone 7 — Pascal language completeness** is complete (`0.7.0`).
Toolchain version is `0.7.0` (`PROJECT_VERSION`); cut git tag `v0.7.0` when ready.
Multi-language expansion remains [deferred](docs/milestones/08-multi-language-expansion.md)
as Milestone 8.

## Supported dialect

Apollo targets a **Wirth console / Pascal80-style** subset suitable for standalone Gemini
VM programs:

**In scope:** `program` / `begin`…`end`; `const` / `type` / `var`; integer, real, boolean,
char, string; flat `record`; `array [lo..hi] of T` (const bounds); value array parameters
and whole-array assign via `MAT_COPY`; procedures and functions (program-block only);
`if` / `while` / `repeat` / `for` / `case`; `write` / `writeln` / `read` / `readln`;
`{$I}` / `{$i}` file includes (path relative to the includer; nesting depth ≤ 32).

**Out of scope:** units; `file` / file I/O; pointers; sets; nested subprograms; enclosing-scope
locals; `var` array parameters; `with` / `goto`; jump-table `case`; most other `{$…}`
directives (unknown directives warn and are ignored).

See **[docs/](docs/README.md)** for the overview and milestone plan.

## Building

Requires **CMake 3.16+** and a **C++20** toolchain.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Artifacts:

- **`apolloc`** — compiler driver CLI (`--list`, `--tokens`, `--ast`, `--check`, `--ir`, `--emit` / `--tbc`, `--version`, `--help`)
- **`apollo-common`** — shared compiler support (source buffer, listing, diagnostics)
- **`apollo-ir`** — shared language-neutral intermediate representation
- **`apollo-codegen`** — Gemini `.tbc` text bytecode writer and IR emitter
- **`apollo-pascal`** — Pascal scanner, parser, semantic analysis, IR lowering, and dump helpers

### Listing a source file

```bash
./build/src/tools/apolloc --list examples/hello.pas
```

Prints a numbered listing (width-4 line numbers), for example:

```text
   1: program Hello;
   2: begin
   3:   writeln('Hello, Gemini!');
   4: end.
```

### Dumping tokens

```bash
./build/src/tools/apolloc --tokens examples/hello.pas
```

Scans Pascal source and prints one token per line (`line:col-endLine:endCol  Kind  lexeme`).
Lexical errors go to stderr; the process exits non-zero if any errors were reported.

### Dumping the AST

```bash
./build/src/tools/apolloc --ast examples/hello.pas
```

Scans and parses Pascal source, runs semantic analysis, and prints an indented AST.
Diagnostics go to stderr; the process exits non-zero if any errors were reported.

### Semantic check

```bash
./build/src/tools/apolloc --check examples/hello.pas
```

Scans, parses, and semantically analyses Pascal source (no AST dump). Diagnostics go
to stderr; exit 0 means the program is free of lexical, syntactic, and semantic errors
reported by the analyser. See [`examples/README.md`](examples/README.md) for a catalog
of sample programs.

### Dumping IR

```bash
./build/src/tools/apolloc --ir examples/hello.pas
./build/src/tools/apolloc --ir examples/count.pas
```

Scans, parses, analyses, and lowers Pascal source to the shared Apollo IR, then prints
a human-readable IR dump to stdout. Diagnostics go to stderr; the process exits
non-zero if any errors were reported, and skips the IR dump when errors are present.

### Emitting bytecode

```bash
./build/src/tools/apolloc --emit examples/hello.pas
./build/src/tools/apolloc --tbc examples/count.pas
```

Scans, parses, analyses, lowers, and emits Gemini `.tbc` text bytecode to stdout.
`--tbc` is an alias for `--emit`. Diagnostics go to stderr; the process exits
non-zero if any errors were reported, and skips the `.tbc` dump when errors are present.

## Layout

```
apollo-compiler/
  docs/           Project overview and milestones
  include/apollo/ Public headers
  src/
    common/       Shared support (source, listing, diagnostics)
    pascal/       Pascal front-end pipeline (scanner → codegen)
    runtime/      Host-agnostic runtime pieces (later milestones)
    tools/        Command-line tools (apolloc)
  tests/
  examples/       Sample Pascal programs (see examples/README.md)
```

## Relationship to Gemini

- **Gemini** defines the VM instruction set, `.tbc` format, and Pick-hosted tooling
  (including an in-tree BASIC compiler). The Pick-independent **`gemini-vm`** runner
  executes Apollo `.tbc` with console I/O (Milestone 6).
- **Apollo** aims for clean, modular front-ends sharing a common IR and Gemini backend.
  Near-term focus is Pascal language completeness (Milestone 7); further languages are
  deferred (Milestone 8).

Emit **`.tbc` text** so programs load on Gemini’s existing bytecode parser without a hard
build-time link to `gemini-core`. Linking against PickVM for in-process tests remains an
option later.

## License

See [LICENSE](LICENSE).
