# Pascal file handling

Design notes for Pascal durable storage in Apollo. Supported Track A behaviour is also
summarized in [`pascal-dialect.md`](pascal-dialect.md).

Delivery is split into **two tracks**. Track A is [Milestone 8](milestones/08-pascal-runtime-library.md)
Stage 3 (**completed**).

| Track | What | Owner / timing |
|-------|------|----------------|
| **A — Wirth files** | `text`, `file of T`, sequential I/O via Gemini **hostfs** (`CALL_FUNC` ns 7) | Milestone 8 Stage 3 — **shipped** |
| **B — Pick database API** | Explicit Pick module (e.g. `PickDB`): `PickFile`, open/read/write/select, attr helpers | [Milestone 9](milestones/09-pick-database-api.md) — **not** M8 Stage 3 |

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

Gemini M22 **hostfs** façade (`CALL_FUNC` namespace **7**, module `gemini-module-hostfs`) —
met. Line buffering, `eof`, and `eoln` are implemented in Apollo.

### Supported surface

| Area | Stage 3 lock |
|------|----------------|
| Types | `text`; `file of T` where `T` is `integer` / `real` / `boolean` / `char` or a **flat record** of those. Bare `file`, array components, nested-record components, and `string` components are **diagnosed**. |
| Path binding | Both `assign(f, path)` then `reset`/`rewrite(f)`, and `reset(f, path)` / `rewrite(f, path)` (path form implies assign). |
| Open / close | `reset` → OpenRead; `rewrite` → OpenWrite (truncate); `close` |
| Text transfer | `read` / `readln` / `write` / `writeln` with optional first-arg file; buffered 1-byte hostfs reads for `eof`/`eoln` |
| Typed transfer | Whole-component `read(f, x)` / `write(f, x)` only (`readln`/`writeln` illegal on typed files) |
| Status | `eof(f)` (text and typed); `eoln(f)` (text only) |

### Binary / component layout

| Component | Encoding |
|-----------|----------|
| `integer`, `boolean` | 4-byte little-endian (`boolean` as `0`/`1`) |
| `char` | 1 byte |
| `real` | 24-byte ASCII via BASIC `STR` / `COERCE_FLT` (no VM float-bitcast); not IEEE754 binary |
| flat `record` | Fields in declaration order, concatenated |

### Non-goals (Stage 3)

- Turbo `BlockRead` / `BlockWrite`; random access; `get`/`put` as separate names
- Pick / MultiValue API (Track B / Milestone 9)
- Field widths (Milestone 10)
- Compile-time diagnosis when hostfs is unbound (runtime `HOSTFS:` errors)

### Runtime

Emit binds `CALL_FUNC 7, <id>, <arity>` for hostfs. File programs need
`gemini-vm --modules <dir>` (or `GEMINI_MODULES_PATH`) including **hostfs**; text numeric
I/O also uses **basic** `STR`/`LEN`.

### Examples

See [`examples/filetext.pas`](../examples/filetext.pas) and
[`examples/filetyped.pas`](../examples/filetyped.pas).

---

## Track B — Pick database API ([Milestone 9](milestones/09-pick-database-api.md))

### Goal

Authentic Pick / MultiValue access for applications that need files, item IDs, records,
attributes, values, and select lists — without pretending those are stream `file of T`.
This is the next numbered milestone after M8 Stage 3, and comes **before** optional dialect
polish (M10) and multi-language expansion (M11).

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
  ├── Track A: text / file of T  →  Gemini hostfs (CALL_FUNC ns 7)  →  host backend
  └── Track B: PickDB (M9)       →  Gemini DB / Pick plugin         →  VOC/MD (etc.)
```

- **M8 Stage 3** implements Track A only (**completed**).
- **Milestone 9** implements Track B (PickDB).
- “Pascal files on Pick?” remains open at the façade/capability layer, not as a
  language fork.
- Field widths / string helpers: optional [Milestone 10](milestones/10-pascal-dialect-polish.md).
