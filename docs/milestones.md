# Project milestones

This document is an **orienting roadmap**. Priorities and scope may change as Apollo and
the Gemini System evolve.

## Milestone 0 — Project skeleton (current)

- Repository structure
- Build system (CMake)
- Shared compiler support library stub (`apollo-common`)
- Driver CLI skeleton (`apolloc`)
- Overview and milestone documentation

**Primary early deliverables after M0:** token definitions, scanner, source abstraction,
listing utility, parser skeleton, AST framework.

## Milestone 1 — Source & scanner infrastructure

- Program listing utility
- Source file abstraction
- Scanner (Pascal)
- Token stream
- Diagnostic subsystem

## Milestone 2 — Parser

- Recursive descent parser
- AST node definitions
- Basic symbol table
- Syntax error handling

## Milestone 3 — Semantic analysis

- Type checking
- Scope rules
- Full symbol table
- Semantic error reporting

## Milestone 4 — Intermediate representation

- Language-neutral IR design
- IR generator (from Pascal semantic AST)
- Control flow representation

## Milestone 5 — Code generator

- Gemini VM backend (prefer `.tbc` emission first)
- Calling conventions
- Runtime library integration

## Milestone 6 — Standalone VM execution

- Host abstraction layer
- VM runtime usable outside Gemini Pick
- First Apollo-compiled Pascal program running natively on a host OS

*Note:* Gemini already hosts a bytecode VM on desktop OSes. This milestone is about
**decoupling** a portable runtime/ABI from the Pick shell and filesystem so Apollo
programs can run without the full Gemini environment—not rewriting the VM from scratch
unless Gemini’s runtime cannot be shared cleanly.

## Milestone 7 — Multi-language expansion

- BASIC front-end
- COMAL front-end
- Fortran front-end
- COBOL front-end

Front-ends share the IR and Gemini backend established in Milestones 4–5.

## Suggested near-term sequence after M0

1. Source buffer + locations + diagnostics in `src/common`
2. Pascal token kinds + scanner + listing tool
3. Parser skeleton and AST framework
4. Minimal end-to-end path: trivial Pascal program → `.tbc` → run under Gemini
