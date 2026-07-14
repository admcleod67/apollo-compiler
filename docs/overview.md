# Apollo Compiler overview

## Purpose

Apollo is a **multi-language compiler toolchain** that targets the **Gemini Virtual Machine**.
It is a sister project to **gemini-system**: Gemini provides the Pick-inspired host environment
and bytecode VM; Apollo provides portable language front-ends and, over time, a host-agnostic
runtime path for Gemini applications outside the Gemini Pick OS.

**Initial language:** Pascal  
**Long-term languages:** BASIC, COMAL, Fortran, COBOL

## High-level goals

- Build a clean, modular compiler suite that targets the Gemini VM.
- Drive evolution of the Gemini VM into a portable, host-agnostic runtime.
- Establish a stable ABI, instruction set contract, and runtime library surface for the VM.
- Provide a foundation for multiple language front-ends sharing a common IR and backend.

## Core architectural principles

- **Front-end first:** scanner → parser → semantic analysis → IR → codegen.
- **VM-centric design:** all languages compile to a shared intermediate representation and
  a Gemini backend.
- **Modularity:** separate components for scanning, parsing, AST, semantic analysis, IR,
  codegen, and runtime.
- **Portability:** the VM must run on non-Gemini hosts (Linux, Windows, macOS).
- **Clean separation:** compiler front-ends are host-agnostic; the VM runtime abstracts
  platform differences.

## Pipeline (target shape)

```mermaid
flowchart LR
    source[Source] --> scanner[Scanner]
    scanner --> parser[Parser]
    parser --> ast[AST]
    ast --> semantic[Semantic analysis]
    semantic --> ir[Shared IR]
    ir --> codegen[Gemini codegen]
    codegen --> tbc[.tbc / Instruction image]
```

Pascal is the first front-end. Later languages plug in at the scanner/parser/AST layers and
reuse the shared IR and Gemini backend.

## Gemini integration (early decision)

For early milestones, Apollo prefers emitting **Gemini `.tbc` text bytecode**. That keeps
coupling loose: Gemini’s existing parser and runtime can execute Apollo output without
requiring Apollo to link against `gemini-core` at build time.

In-process linking against PickVM (emitting `Instruction` vectors directly) may be added
later for tighter integration tests.

Gemini’s in-tree BASIC compiler and its language-specific normalized IR are useful
*references* for what the VM needs; Apollo’s shared IR is intended to be language-neutral
and owned by this project.

## Attribution

Apollo Compiler is an original open-source project whose architecture and development
approach are inspired by Ronald Mak’s *Writing Compilers and Interpreters*. No code or
text from the book is used directly; only the conceptual methodology informs the design.

## See also

- [Milestones](milestones.md) — roadmap index
- [Milestone 1 — Source & scanner](milestones/01-source-and-scanner-infrastructure.md) — completed (M1)
- [Milestone 2 — Parser](milestones/02-parser.md) — completed (M2)
- Gemini VM documentation: `gemini-system/docs/vm.md` (sibling repository)
