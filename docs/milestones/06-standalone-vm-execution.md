← [Project milestones index](../milestones.md)

## Milestone 6 — Standalone VM execution

- Host abstraction layer
- VM runtime usable outside Gemini Pick
- First Apollo-compiled Pascal program running natively on a host OS

*Note:* Gemini already hosts a bytecode VM on desktop OSes. This milestone is about
**decoupling** a portable runtime/ABI from the Pick shell and filesystem so Apollo
programs can run without the full Gemini environment—not rewriting the VM from scratch
unless Gemini’s runtime cannot be shared cleanly.
