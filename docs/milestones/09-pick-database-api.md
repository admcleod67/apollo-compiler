← [Project milestones index](../milestones.md)

## Milestone 9 — Pick database API (Pascal)

This document defines **Milestone 9**: bind Apollo Pascal to Gemini Pick’s MultiValue
database surface via an explicit module API (working name **`PickDB`**), so Pascal is
usable as a Pick application language without emulating Pick files as Wirth `file of T`.

It follows:

- [Milestone 8 — Pascal runtime library](08-pascal-runtime-library.md) (Wirth console +
  Track A file I/O)
- [Pascal file handling](../pascal-file-io.md) **Track B**
- And **defers** further Pascal polish to
  [Milestone 10](10-pascal-dialect-polish.md) and other source languages to
  [Milestone 11](11-multi-language-expansion.md)

**Priority:** after Milestone 8 Stage 3 (or with Stage 3 explicitly blocked/documented),
complete the Gemini Pick story for Pascal before multi-language expansion.

### Goals (summary)

- Ship Pascal types and routines for Pick files, item IDs, records, select lists, and
  attribute/value helpers — backed by Gemini’s Pick / DB plugin, not the Wirth
  file-service façade.
- Keep the language model separate from platform integration: Wirth `text` / `file of T`
  remain Track A (M8 Stage 3); Pick access is explicit.
- Diagnose clearly when the PickDB module or host DB services are unavailable.
- Leave open whether a Pick host also binds the Wirth file-service façade (runtime
  capability, not a dialect fork).

### Baseline (end of M8 intent)

| Area | Status |
|------|--------|
| Console + Stage 1 / 1b / 2 | Shipped |
| Wirth `file` / `text` (Track A) | M8 Stage 3 (façade-gated) |
| Pick MultiValue from Pascal | This milestone |
| Field widths / string helpers | Milestone 10 (optional polish) |
| Second languages | Milestone 11 (deferred) |

---

## In scope / out of scope

**In scope (M9)**

- Module (or published `CALL_FUNC` namespace) for PickDB: e.g. `PickFile`, `PickSelectList`,
  `PickOpen` / `PickRead` / `PickWrite` / `PickDelete`, `PickSelect` / `PickReadNext`,
  and attr/value helpers (`GetAttr` / `SetAttr` / `GetValue`, exact set pinned in Stage
  notes).
- Analyse / lower / emit bindings; dialect + [`pascal-file-io.md`](../pascal-file-io.md)
  Track B updated to “supported” where shipped.
- Examples and tests that run on a Pick-backed Gemini host with the module loaded.

**Explicitly deferred**

- Implementing or requiring Wirth files on Pick hosts (façade capability — open).
- Replacing BASIC’s native Pick builtins; optional later convergence on shared DB ops.
- Field widths, Turbo string helpers, units, sets, pointers
  ([Milestone 10](10-pascal-dialect-polish.md) and backlog).
- Additional source languages ([Milestone 11](11-multi-language-expansion.md)).

---

## Suggested staged delivery

### Stage 1 — ABI + open/read/write/delete

**Objective:** Minimal round-trip: open a Pick file, read/write/delete an item as string
data.

**Prerequisite:** Gemini publishes stable PickDB / DB service IDs and a loadable module
(or equivalent).

**Acceptance criteria**

- [ ] Documented namespace/function IDs; Apollo emit binds them.
- [ ] Wrong arity/types diagnose; missing module/host diagnosed clearly.
- [ ] Example `--emit`s and runs on Pick-backed Gemini.
- [ ] `ctest` green (host-dependent tests documented if skipped in standalone CI).

### Stage 2 — Select lists + attr/value helpers

**Objective:** `PickSelect` / `PickReadNext` and common record field helpers without
exposing mark characters in typical call sites.

**Acceptance criteria**

- [ ] Select iteration and helpers analyse/lower/emit.
- [ ] Dialect / file-handling doc lists the supported PickDB surface.
- [ ] `ctest` / host tests green as appropriate.

---

## Success criteria (draft)

- [ ] Pascal programs can perform core Pick file and select operations via PickDB.
- [ ] Wirth Track A remains separate; no fake `file of T` over Pick items.
- [ ] Multi-language expansion remains deferred as Milestone 11.
- [ ] Dialect overview and [`pascal-file-io.md`](../pascal-file-io.md) stay accurate.
- [ ] `ctest` green on a clean configure/build (with documented Pick host checks).

---

## Implementation status

| Stage | Status |
|-------|--------|
| Stage 1 — Open/read/write/delete | not started (blocked on Gemini PickDB ABI / module) |
| Stage 2 — Select + attr helpers | not started |

---

## Boundary with adjacent milestones

| Concern | M8 | M9 | M10 | M11 |
|---------|----|----|-----|-----|
| Wirth files | Owns (Stage 3) | — | — | — |
| PickDB | Design only | Owns | — | — |
| Field widths / strings | Deferred | Deferred | Owns (optional) | — |
| Second languages | Deferred | Deferred | Deferred | Owns when resumed |

---

## Out of scope reminders

- Do not start Milestone 11 front-ends during M9.
- Do not implement Pick access by overloading Wirth `reset` / `file of T`.
- Keep Gemini as ABI + plugin owner; Apollo owns Pascal bindings and dialect docs.
