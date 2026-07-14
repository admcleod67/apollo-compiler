← [Project milestones index](../milestones.md)

## Milestone 5 — Code generator

- Gemini VM backend (prefer `.tbc` emission first)
- Calling conventions
- Runtime library integration

### Runtime / language-module note

Console builtins (`write` / `writeln` / `read` / `readln`) should lower to Gemini
**`CALL_FUNC`** (or the published equivalent) using namespace/function IDs from Gemini’s
module ABI — not Pick-specific opcodes.

Ownership of the Pascal helper **shared library** (spike in gemini-system vs steady
state built with Apollo) is documented under
[Milestone 6 — Language modules](06-standalone-vm-execution.md#language-modules-pascal-builtins).
