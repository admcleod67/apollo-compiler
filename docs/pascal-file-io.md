# Pascal file handling

Design notes for Pascal durable storage in Apollo. This is **not** a language tutorial;
supported behaviour, once shipped, will also appear in [`pascal-dialect.md`](pascal-dialect.md).

Delivery is split into **two tracks**. Only Track A is [Milestone 8](milestones/08-pascal-runtime-library.md)
Stage 3.

| Track | What | Owner / timing |
|-------|------|----------------|
| **A — Wirth files** | `text`, `file`, `file of T`, sequential I/O against Gemini’s **generic file-service** interface | Apollo M8 Stage 3 (after host FS façade) |
| **B — Pick database API** | Explicit Pick module (e.g. `PickDB`): `PickFile`, open/read/write/select, attr helpers | Later; Gemini Pick + Apollo module binding — **not** Stage 3 |

Pick MultiValue access does **not** replace Track A. Apollo implements the standard Pascal
file model regardless of host. Whether a Pick personality ever **also** exposes Wirth
`text` / `file of T` through the file-service façade is a **runtime capability** decision
and is left open.

---

## Track A — Wirth Pascal files (Milestone 8 Stage 3)

### Goal

Standards-oriented sequential file I/O for teaching and console programs, independent of
Pick storage concepts.

### Prerequisite

A host-agnostic **file-service façade** in Gemini that both the standalone runner and
other hosts can bind (see Milestone 7 — *Why Pascal `file` I/O waits*). Apollo must not
hard-wire POSIX paths or Pick VOC/MD paths in the front-end.

### Language surface (intent)

Exact Stage 3 pinning lives in the Milestone 8 Stage notes when implementation starts.
Intentional core:

| Area | Intent |
|------|--------|
| Types | `text`; `file of T` (scope of bare `file` pinned in Stage notes) |
| Open / create | Sequential `reset` / `rewrite` (filename binding model pinned in Stage notes) |
| Transfer | Typed / text `read` / `write` / `readln` / `writeln` as appropriate |
| Status | `eof` / `eoln` where they apply |
| Non-goals for Stage 3 | Turbo `BlockRead` / `BlockWrite`; random access; packing; units |

Console `read` / `write` remain the existing builtins; file forms are additional once
types exist.

### Runtime contract

- Emit binds to Gemini’s **generic file-service** interface (opcodes and/or `CALL_FUNC`
  IDs as published), not to a Pick-specific ABI.
- If the host does **not** provide the required file services, Apollo should **diagnose**
  (prefer compile-time / capability check over opaque runtime failure). That is a
  capability gate, not a separate “Pick dialect” that removes Pascal file types from the
  language.

### Examples (illustrative)

```pascal
var
  F: text;
  Customers: file of Customer;
begin
  { reset / rewrite / read / write / eof / eoln — exact forms TBD in Stage 3 }
end.
```

---

## Track B — Pick database API (later)

### Goal

Authentic Pick / MultiValue access for applications that need files, item IDs, records,
attributes, values, and select lists — without pretending those are stream `file of T`.

### Why not emulate with `file of T`

Pick records are variable length; data is organised as attributes / values / subvalues;
item IDs and dictionaries are part of the access model; select lists are first-class;
Pick files are not stream-oriented. Mapping that onto `file of RecordType` obscures the
model and couples the Pascal dialect to one storage backend.

### Preferred model

A dedicated module (working name **`PickDB`**) exposing Pick-native types and routines,
backed by the Pick filesystem / database plugin — parallel to how shared **math** is a
module, not a redefinition of language arithmetic.

Illustrative API (names and signatures TBD when the track is scheduled):

```pascal
var
  Customers: PickFile;
  Rec: string;
  List: PickSelectList;
begin
  if PickOpen('CUSTOMERS', Customers) then
    if PickRead(Customers, '100123', Rec) then
      writeln(Rec);
  { PickWrite, PickDelete, PickSelect, PickReadNext, GetAttr / SetAttr / GetValue, … }
end.
```

### Shared VM services (Gemini)

Language wrappers should sit on common record-management services usable by BASIC,
Pascal (`PickDB`), and later languages — for example conceptual ops such as `DB.OPEN`,
`DB.READ`, `DB.WRITE`, `DB.DELETE`, `DB.SELECT`, `DB.READNEXT` (exact ABI is Gemini’s).

Track B does **not** require disabling Track A on Pick hosts. Programs that need the
database use `PickDB`; programs that need Wirth files use Track A **if** the host
exposes the file-service façade.

---

## Relationship summary

```text
Apollo Pascal language
  ├── Track A: text / file of T  →  Gemini file-service façade  →  host backend
  └── Track B: PickDB (later)    →  Gemini DB / Pick plugin      →  VOC/MD (etc.)
```

- **Stage 3** implements Track A only.
- Track B is platform integration, scheduled separately.
- “Pascal files on Pick?” remains open at the façade/capability layer, not as a
  language fork.
