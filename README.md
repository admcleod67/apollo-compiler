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

**Milestone 0 — Project skeleton** is complete. **Milestone 1 — Source & scanner
infrastructure** is current; see
[`docs/milestones/01-source-and-scanner-infrastructure.md`](docs/milestones/01-source-and-scanner-infrastructure.md).

See **[docs/](docs/README.md)** for the overview and milestone plan.

## Building

Requires **CMake 3.16+** and a **C++20** toolchain.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Artifacts:

- **`apolloc`** — compiler driver CLI (`--list`, `--version`, `--help`)
- **`apollo-common`** — shared compiler support library (source buffer + listing)

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

## Layout

```
apollo-compiler/
  docs/           Project overview and milestones
  include/apollo/ Public headers
  src/
    common/       Shared support (source buffer, listing; diagnostics forthcoming)
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
