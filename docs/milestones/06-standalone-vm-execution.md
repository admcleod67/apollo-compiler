← [Project milestones index](../milestones.md)

## Milestone 6 — Standalone VM execution

This document defines **Milestone 6**: run Apollo-compiled programs on a host OS
without the full Gemini Pick environment, by decoupling a portable VM/runtime from
Pick-specific shell and filesystem assumptions.

It complements:

- [Project milestones](../milestones.md)
- [Overview](../overview.md) (portability and Gemini integration)
- [Milestone 5 — Code generator](05-code-generator.md) (emits Gemini bytecode Apollo will run)
- Sister project **gemini-system** (owns the VM and host abstraction work). On the Gemini side this is tracked as **Milestone 19 — Standalone VM Runner** (`docs/milestones/19-standalone-vm-runner.md` in the gemini-system / pick-system tree).

### Goals (summary)

- Host abstraction layer for console (and later filesystem) services.
- VM runtime usable outside Gemini Pick.
- First Apollo-compiled Pascal program running natively on a host OS
  (e.g. Linux / macOS / Windows).

### Starting point

Gemini already hosts a bytecode VM on desktop OSes. This milestone is about
**decoupling** a portable runtime/ABI from the Pick shell and filesystem so Apollo
programs can run without the full Gemini environment — **not** rewriting the VM from
scratch unless Gemini’s runtime cannot be shared cleanly.

---

## Ownership and evolution

### Where the work lives

| Area | Primary home |
|------|----------------|
| VM core, module loader / ABI, host FS extraction | **gemini-system** |
| Emit bytecode (`.tbc` / Instruction image) | **apollo-compiler** (Milestone 5) |
| Pascal builtin helpers (console I/O module) | See **Language modules** below |
| Prove an Apollo Pascal program runs on the standalone target | **Both** — Gemini provides the runner; Apollo provides the program |

Milestone 6 is **mostly Gemini-side**. Apollo’s success criterion is that a program it
compiled runs on that portable target — not that Apollo owns or reimplements the VM.

### Language modules (Pascal builtins)

Gemini’s multi-language runtime (e.g. Milestone 11 / `CALL_FUNC` + boot-time module
loader) is designed so **new languages do not require rebuilding the VM**. Modules are
discovered and `dlopen`’d from a configured directory; Gemini publishes the ABI and
namespace/function IDs. Apollo codegen emits `CALL_FUNC` for builtins against that
contract.

**v1 builtins in Apollo today** are console only: `write`, `writeln`, `read`, `readln`.
There is no Pascal math library (`sin`, `sqrt`, …) yet. User `procedure` / `function`
code is ordinary program code inside `.tbc`, not language-module entry points.

| Horizon | Where the Pascal I/O module lives |
|---------|-----------------------------------|
| **M6 spike** | Acceptable to implement a minimal Pascal (or shared) I/O module **in gemini-system** so `gemini-vm` + hello/count can prove the path without waiting on Apollo packaging |
| **Steady state** | Build and ship the Pascal helper module with **apollo-compiler** (e.g. under `src/runtime/`) against Gemini’s published module ABI; install into Gemini’s module path. Gemini remains **ABI + loader**, not the catalogue of every outside language’s implementations |

Rationale: keeping every front-end’s builtins forever inside gemini-system couples
releases and makes external languages second-class. Drop-in modules keep Pick/Gemini
from needing each language at **build** time — only at **deploy** time (module on disk
+ matching IDs in bytecode).

### Evolution over a third “Mercury” repo

A separate Mercury project (shared VM for Gemini and Apollo) was considered and
**deferred**.

Prefer evolving Gemini in place:

1. Keep improving the existing pluggable language-library surface (BASIC is the first
   implementation; Apollo/Pascal is a later consumer of the same runtime contract).
2. Extract filesystem handling into a library so the VM is no longer Pick-VOC/MD-hardwired.
3. Wire a host-only build that uses VM + language plugin + thin console (and later FS)
   hooks **without** the Pick shell.

Revisit a Mercury-style split only if a clean portable package exists and Apollo still
cannot depend on gemini-system without dragging Pick IDE/OS concerns. Until then,
Gemini **embeds** a disentangled runtime; it is composed, not incomplete.

---

## Approach: review, then spike

Milestone 6 should start with a **review and spike**, not a full rewrite:

1. **Review** — Can the current VM (and plugin model) run in a host-only process with
   console hooks alone? What remains Pick-specific (filesystem, accounts, IDE, path
   layout)?
2. **Spike** — After Milestone 5, run an Apollo-emitted program that only needs console
   I/O (e.g. `examples/hello.pas` / `examples/count.pas`). If a thin driver can load and
   execute it, that is enough for the first “native on a host OS” proof.
3. **Decide** — If the spike succeeds, treat that as M6 success and schedule deeper FS
   extraction as follow-on Gemini work. If not, the review becomes the backlog for
   disentangling.

---

## Why console-only Pascal helps

Apollo’s v1 Pascal subset (Milestones 2–3) treats `write` / `writeln` / `read` /
`readln` as ordinary calls and **defers Pascal file I/O**. That is intentional leverage
for Milestone 6:

- The first standalone proof does **not** require Pick filesystem semantics.
- Host hooks can start with stdin/stdout (and minimal runtime for scalars / strings).
- Filesystem-as-library work in Gemini can proceed on its own schedule; Apollo can add
  file I/O later once that façade exists.

Language plugins should eventually share the same host façade (console, open/read/write,
alloc) so BASIC and Pascal do not each bake Pick paths into the plugin.

---

## In scope / out of scope

**In scope**

- Portable runner / library build in gemini-system (or clearly exported from it).
- Review + spike against Apollo-produced bytecode.
- Document ABI / host-hook assumptions needed by Apollo M5 output.
- First Pascal program (console-only) running outside Pick.

**Out of scope (default)**

- Creating a Mercury repository in this milestone.
- Full Pick OS replacement or rewriting the VM from scratch.
- Apollo reimplementing a second VM in `apollo-compiler`.
- Pascal file I/O as a requirement for the first native proof.

---

## Success criteria (draft)

- [ ] Review recorded: Pick vs portable boundaries for the current VM.
- [ ] Host-only (or clearly portable) build target exists in gemini-system.
- [ ] An Apollo-compiled Pascal program with console I/O only runs on that target.
- [ ] Apollo docs / README note how to run the standalone path once available.

---

## Follow-on (beyond M6)

- Filesystem library extraction in gemini-system.
- Richer host I/O for both BASIC and future Pascal file support.
- Move Pascal I/O module ownership to apollo-compiler if the M6 spike lived in
  gemini-system (steady-state drop-in module).
- Optional later extraction of the portable runtime into a shared package/repo if
  cross-project dependency pain justifies it.
