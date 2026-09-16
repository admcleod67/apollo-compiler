# Apollo consumer notes — optional VM / runtime improvements

**Author:** Apollo Compiler — Pascal front-end, shared IR, Gemini `.tbc` text codegen.

**Status:** suggestions only. Nothing here is required for a complete Pascal dialect on the
current VM, and nothing here is required for correct execution of today’s Apollo-emitted
programs on the standalone runner.

---

## Purpose

Apollo targets the Gemini VM by emitting **text `.tbc`**. Where the runtime model is awkward
for *any* language front-end, Apollo implements **workarounds in the compiler**. Those
workarounds are stable and tested; the items below are **optional evolution** ideas that
could simplify bytecode, improve performance, or unlock features front-ends may want later.

This file is a **consumer backlog**, not a specification change. **Normative VM behavior**
remains whatever the VM project documents as authoritative for opcodes and runtime semantics.

---

## How to read the backlog

Each theme includes:

| Field | Meaning |
|-------|---------|
| **Friction** | What makes front-ends or runtimes work harder than necessary |
| **Typical pattern today** | What Apollo (or similar emitters) do on the current VM |
| **Possible direction** | General VM/host improvement (not Pascal-only unless noted) |
| **Benefit** | Who gains — usually cross-language |
| **Priority** | Rough tier: *capability*, *ergonomics*, *performance* |

Emitters should treat behavior changes as **versioned VM improvements**: agree semantics,
ship in runners, then optionally simplify compiler output in a separate change.

---

## 1. Array storage, copy, and call boundaries

**Friction**

- Scalar variables and arrays often behave as **separate storage** (for example passing an
  array name where a scalar load is expected fails at run time).
- **`MAT_COPY`** requires the **destination array to already exist** (dimensioned).
- **`DIM_ARRAY`** on an existing array **re-dimensions and wipes** contents.

**Typical pattern today (Apollo)**

- Whole-array assign and record array fields: `MAT_COPY dst|src` between mangled slot names.
- **Value array parameters:** at each call site, before `CALL`:
  `DIM_ARRAY` callee formal → `MAT_INIT` formal → `MAT_COPY formal|actual`. The callee does
  **not** re-dim array parameters in its entry prologue (only locals). This convention
  avoids copying into a missing formal and avoids callee prologue re-dim wiping a copy
  performed before the call.

**Possible direction**

- **Clearer array value semantics at calls:** optional ABI support (for example copy or
  bind actual→formal as part of `CALL`, or a single “ensure dim + copy” opcode) so callers
  need not emit a full dim/init/copy sequence every time.
- **`MAT_COPY` convenience:** create or resize `dst` when missing or smaller (with defined
  rules), or document a dedicated “copy into slot” op with explicit size.
- **Safer re-dim:** idempotent dim when size unchanged, or split “allocate” vs “clear” so
  copies are not accidentally destroyed by callee prologues.
- **Unified naming / handles:** optional indirection so array formals are not only
  persistent global-like slots — without requiring Pascal-specific syntax in the VM.

**Benefit**

- Smaller `.tbc`, faster calls, less convention drift between caller and callee.
- Helps any language with array value parameters or bulk copy, not only Pascal.

**Priority:** *performance* and *ergonomics* (Apollo is already correct without this).

---

## 2. Array reference parameters (`var` / by-reference)

**Friction**

- **Copy semantics** (`MAT_COPY`) implement **value** parameters only. **Reference**
  parameters need shared storage: mutations in the callee must be visible to the caller.

**Typical pattern today (Apollo)**

- `var` array parameters are **rejected at analyse** until a reference model exists.

**Possible direction**

- **Array aliases or handles** in the VM (two names, one backing store), or documented
  `var` parameter convention at `CALL` (for example pass slot name / handle, not copy).
- Clear interaction with `LOAD_ARR` / `STORE_ARR` and bounds checking on the shared store.

**Benefit**

- Standard for Pascal, BASIC-style arrays, and other Wirth-family languages.

**Priority:** *capability* (new feature class, not a fix for current Apollo output).

---

## 3. Numeric operations and types

**Friction**

- **No core integer `MOD` opcode** — emitters expand to `div` / `mul` / `sub` (or
  language-module calls).
- **Division type** follows **runtime operand types** (integer vs double on stack), which
  pushed Apollo to explicit **`Integer → Real` widening** in IR (`ConvertF64`, or
  multiply by `1.0`) rather than relying on mixed-type opcodes.
- **Real input:** no dedicated float input opcode; `INPUT_STR` plus parse is a common
  pattern.
- **`PUSH_INT` parsing** uses host integer parsing (emitters document edge cases for very
  large bound constants in index remapping).

**Typical pattern today (Apollo)**

- `mod` → expanded stack sequence (Turbo-style truncated division semantics).
- Real contexts widen integers before `/` and other real operations.
- `readln(real)` → read string, parse to double.

**Possible direction**

- Optional **`MOD`** (or **`IMOD`**) with documented truncated vs floored semantics.
- Optional **`COERCE_FLT`** / explicit widen opcode to reduce boilerplate in `.tbc`.
- **`INPUT_FLT`** or typed input helpers aligned with `INPUT_INT` / `INPUT_STR`.
- Document or stabilize **mixed-type arithmetic rules** if opcodes should not depend on
  implicit stack typing.

**Benefit**

- Shorter bytecode, clearer semantics for all numeric front-ends.

**Priority:** *ergonomics* (quality of life; Apollo already emits working sequences).

---

## 4. Console I/O, formatting, and language modules

**Friction**

- A **bootstrap** path maps Pascal `write` / `writeln` / `read` / `readln` to core
  **`PRINT_*` / `INPUT_*` / `PRINT_EOL`** opcodes.
- **`PRINT_VAL`** formatting may not match Pascal field widths or default real formatting
  (known dialect deviations on the Apollo side).
- A steady-state direction for multi-language runtimes: **`CALL_FUNC`** plus **drop-in
  language modules** with published namespace and function IDs.

**Typical pattern today (Apollo)**

- Console builtins via an opcode binding table; no Pascal shared library required for
  minimal hello-world programs on the standalone runner.

**Possible direction**

- Publish stable **namespace / function IDs** for a Pascal (or shared) I/O module.
- Optional **formatting hooks** for reals, widths, and `write` vs `writeln` semantics.
- Keep **bootstrap opcodes** for minimal programs without modules installed.

**Benefit**

- Cleaner separation: VM core vs language-specific I/O; better dialect fidelity.

**Priority:** *ergonomics* and *capability*.

---

## 5. Host filesystem and `file` I/O

**Friction**

- Pascal **`file` / `text`** I/O is a **language** feature but needs a **host** storage
  contract. A standalone runner may leave filesystem services unbound; Pick-shaped hosts
  use a different path model.

**Typical pattern today (Apollo)**

- No Pascal file I/O in the supported dialect; waiting on a **host-agnostic filesystem
  façade** before designing opcodes or module calls.

**Possible direction**

- Shared **FS façade** bindable from standalone and Pick backends (paths, open/read/write
  or get/put, errors).
- Later: VM opcodes or module calls that front-ends can target without hard-coding POSIX
  or VOC paths.

**Benefit**

- Unblocks `file of T`, `text`, and record files for Pascal and other languages.

**Priority:** *capability* (large cross-cutting host + VM effort).

---

## 6. Diagnostics and developer experience

**Friction**

- Array bounds traps and similar errors surface **mangled compiler slot names** (for
  example `MAIN$A`), which is correct but opaque to source-level debugging.

**Possible direction**

- Optional **debug metadata** channel (separate from core opcode semantics): source file,
  line, or logical name map embedded in `.tbc` or a sidecar, consumed by the runner for
  error messages.

**Benefit**

- All compiled languages; no change required to core arithmetic/array semantics.

**Priority:** *ergonomics* (optional enhancement).

---

## Non-goals (Apollo’s perspective)

- **Pascal-only opcodes** that do not generalize to other front-ends, unless they are thin
  sugar over general mechanisms.
- **Breaking changes** to existing `.tbc` without version negotiation — Apollo keeps
  regression tests and golden fixtures tied to current opcode behavior.
- **Replacing authoritative VM documentation** — consumer notes inform backlog only.

---

## After an improvement ships

The VM project documents new behavior in its normative spec. Compiler projects may then,
in separate work:

- Shorten emit (for example fewer opcodes before `CALL`).
- Enable previously diagnosed features (for example `var` array parameters).
- Adjust tests to match the new contract.

No Apollo release should **require** the changes listed in this document.

---

## Revision history

| Date | Summary |
|------|---------|
| 2026-03 | Initial consumer backlog (optional VM simplifications; array value params use call-site dim/init/copy on today’s VM). |
