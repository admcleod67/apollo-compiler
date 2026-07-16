# Apollo Compiler

A multi-language **compiler toolchain** targeting the **Gemini Virtual Machine (VM)**.

Apollo is a sister project to [gemini-system](../gemini-system): Gemini hosts the Pick-inspired
environment and bytecode VM; Apollo builds portable language front-ends that compile to that VM.

| | |
|---|---|
| **Initial language** | Pascal |
| **Later languages** | BASIC, COMAL, Fortran, COBOL |
| **Target** | Gemini VM bytecode (`.tbc` text format preferred for early integration) |

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
**Milestone 5 Stages 1–2** (shared `.tbc` writer and straight-line IR emission) are
complete; Stages 3–4 of
[Milestone 5 — Code generator](docs/milestones/05-code-generator.md)
are next. Toolchain version is `0.5.0` (`PROJECT_VERSION`).

See **[docs/](docs/README.md)** for the overview and milestone plan.

## Building

Requires **CMake 3.16+** and a **C++20** toolchain.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Artifacts:

- **`apolloc`** — compiler driver CLI (`--list`, `--tokens`, `--ast`, `--check`, `--ir`, `--version`, `--help`)
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
reported by the analyser.

### Dumping IR

```bash
./build/src/tools/apolloc --ir examples/hello.pas
./build/src/tools/apolloc --ir examples/count.pas
```

Scans, parses, analyses, and lowers Pascal source to the shared Apollo IR, then prints
a human-readable IR dump to stdout. Diagnostics go to stderr; the process exits
non-zero if any errors were reported, and skips the IR dump when errors are present.

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
  examples/
```

## Relationship to Gemini

- **Gemini** defines the VM instruction set, `.tbc` format, and Pick-hosted tooling
  (including an in-tree BASIC compiler).
- **Apollo** aims for clean, modular front-ends sharing a common IR and Gemini backend,
  and for driving a portable VM runtime usable outside the Gemini Pick OS
  (see Milestone 6 in `docs/milestones.md`).

Early codegen will prefer emitting **`.tbc` text** so programs can be loaded and run with
Gemini’s existing bytecode parser without a hard build-time link. Linking against PickVM
for in-process tests remains an option later.

## License

See [LICENSE](LICENSE).
