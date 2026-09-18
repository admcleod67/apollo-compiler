# Apollo Pascal dialect

Overview of the **supported Pascal language** in Apollo: what you can write, how it
behaves, and what is out of scope. This is not a language tutorial or a full reference
manual.

Apollo targets a **Wirth console / Pascal80-style** subset for standalone Gemini VM
programs. Pascal80 and early Turbo Pascal console programs are **inspiration**, not a
compatibility claim.

For sample programs, see [`examples/README.md`](../examples/README.md). For how the
dialect was delivered, see
[Milestone 7](milestones/07-pascal-language-completeness.md). Runtime library growth
(standard functions, console fidelity, file I/O) is
[Milestone 8](milestones/08-pascal-runtime-library.md).

---

## Reserved words

These identifiers are **keywords** (case-insensitive). They cannot be used as names for
programs, types, variables, constants, or subprograms.

**Used in this dialect:**  
`and` `array` `begin` `case` `const` `div` `do` `downto` `else` `end` `for` `function`
`if` `mod` `not` `of` `or` `procedure` `program` `record` `repeat` `then` `to` `type`
`until` `var` `while`

**Reserved but not supported** (diagnosed or rejected when used as language features):  
`file` `goto` `in` `label` `nil` `packed` `set` `with`

**Predeclared names** (ordinary identifiers, not scanner keywords):  
`integer` `real` `boolean` `char`, the console builtins `write` `writeln` `read` `readln`,
and the Stage 1 standard functions `ord` `chr` `succ` `pred` `odd` `abs` `sqr` `trunc`
`round` (see [Standard functions](#standard-functions)).

---

## Program shape

- A single compilation unit is one `program` … `begin` … `end.`
- Declarations: `const`, `type`, `var`, then optional `procedure` / `function` blocks,
  then the main body.
- Subprograms may be declared only in the **program block**. Nested procedures or
  functions inside a subprogram are diagnosed.
- A subprogram cannot read or write `var` / `param` of an enclosing subprogram (no
  up-level locals).

---

## Types and values

| Kind | Notes |
|------|--------|
| `integer` | 32-bit host/`int`-range constraints apply to array bounds and some literals |
| `real` | IEEE-754 `double` range; out-of-range literals are diagnosed |
| `boolean` | |
| `char` | |
| String literals | Usable with `write` / `writeln` and as string array elements |
| Type aliases | `type t = …` |
| `array [lo..hi] of T` | Bounds must be **constant** integers (`lo ≤ hi`); indexable as `a[i]` |
| Flat `record` | Field list; field select `r.f`; nested record *field types* are rejected |

**Assignability (high level):** same canonical type, or **integer → real** widening in
expressions and assignments. Whole-array and whole-record assign use structural rules
(see below).

---

## Arrays

- Bounds are compile-time constants (literals, named consts, parenthesised const
  expressions that evaluate to integers).
- Indexed load/store: `a[i]`, and on record array fields `r.a[i]`.
- Whole-array assign: `a := b` when element types are assignable and **bounds match**.
- Value array parameters: pass an array variable to a value formal of the same shape;
  the argument is copied (callee mutations do not affect the caller).
- `var` array parameters, array function results, and non-identifier array actuals are
  not supported (diagnosed).

Array index range checks are performed by the VM at run time.

---

## Records

- Declare `type point = record x, y: integer; end;` (field names must be unique within
  the record).
- Field assign and load: `p.x := 1`, `i := p.x`.
- Array fields: `r.a[i] := …`, and whole field copy `r.a := s.a` when shapes match.
- Whole-record assign: `q := p` when fields match by name order and types; each array
  field must have the **same bounds** as its counterpart.
- Record parameters and record function results are not supported.

---

## Expressions and operators

- Arithmetic: `+`, `-`, `*`, `/`, `div`, `mod`, unary `-`.
- Relational and boolean: `=`, `<>`, `<`, `<=`, `>`, `>=`, `and`, `or`, `not`.
- Integer and real may mix where integer→real widening applies (for example real
  division `/`).
- **`mod`:** truncated (toward-zero) division remainder, matching Turbo Pascal-style
  behaviour — for example `-7 mod 2` is `-1`. This is intentional for this dialect and
  differs from classic ISO / Wirth `mod` rules.

---

## Statements

| Construct | Support |
|-----------|---------|
| Assignment | `:=` including indexed and field targets |
| Procedure / function call | Including bare zero-argument function names as calls |
| `if` … `then` … [`else`] | |
| `while` … `do` | |
| `repeat` … `until` | |
| `for` … `to` / `downto` … `do` | Control variable must be an assignable integer (or compatible) local/param |
| Compound `begin` … `end` | |
| `case` … `of` … [`else`] … `end` | See below |

**`case`**

- Selector: `integer`, `char`, or `boolean` (not `real`, string, array, or record).
- Labels: constants (literals or named consts), comma lists, and `lo..hi` ranges
  (ranges are not used for boolean selectors). Overlapping labels are diagnosed.
- Optional `else`. No fall-through between arms. If nothing matches and there is no
  `else`, execution continues after `end`.

---

## Subprograms

- `procedure` and `function` at program level.
- Parameters: value scalars; value arrays of matching shape; `var` scalars.
- Function result: assign to the function’s own name (`f := …`).
- Not supported: nested subprograms; enclosing-scope access; `var` arrays; record or
  array results; record parameters.

---

## Standard I/O

Builtins: `write`, `writeln`, `read`, `readln` (console only).

- Printable arguments for write routines: integer, real, char, string (boolean is not
  accepted as a write argument).
- `read` / `readln` of `real` reads a line as text and parses a floating value.
- **Known deviations:** real output uses the VM’s default numeric formatting (for
  example `1.0` may print as `1`). Pascal-style field widths such as `write(x:8:2)` are
  not supported. Console fidelity is [Milestone 8](milestones/08-pascal-runtime-library.md)
  Stage 2.

---

## Standard functions

Expression-valued Wirth helpers (not valid as statements). Console I/O remains
statement-only (`write` / `writeln` / `read` / `readln`).

| Function | Argument | Result | Notes |
|----------|----------|--------|-------|
| `ord(x)` | `integer`, `char`, or `boolean` | `integer` | `char` → code point; `boolean` → `0`/`1` |
| `chr(n)` | `integer` | `char` | Constant `n` outside `0..255` is diagnosed; other values are unchecked |
| `succ(x)` / `pred(x)` | `integer`, `char`, or `boolean` | same as `x` | Constant `succ(true)` / `pred(false)` diagnosed; variables unchecked |
| `odd(i)` | `integer` | `boolean` | Uses Turbo-style truncated `mod` |
| `abs(x)` / `sqr(x)` | `integer` or `real` | same as `x` | |
| `trunc(x)` | `real` | `integer` | Toward zero |
| `round(x)` | `real` | `integer` | Half away from zero (add `±0.5`, then trunc) |

| Slice | Functions | Status |
|-------|-----------|--------|
| Ordinal / arithmetic (above) | `ord` … `round` | Supported (Milestone 8 Stage 1) |
| Transcendental | `sqrt`, `sin`, `cos`, `arctan`, `ln`, `exp` | Milestone 8 Stage 1b (needs a math module) |
| File status | `eof`, `eoln` | With file I/O (Stage 3); optional console later |

Not planned in Milestone 8: `new` / `dispose`, `pack` / `unpack`, `page`, Turbo string
helpers (`length`, `copy`, `pos`, …). See
[Milestone 8](milestones/08-pascal-runtime-library.md).

---

## Includes and directives

- `{$I filename}` / `{$i filename}` (optional quotes around the name).
- Path is resolved relative to the **including** file’s directory. Any extension is
  allowed (for example `.inc` or `.pas`); Apollo does not rewrite or search alternate
  extensions.
- Max include nesting depth **32**. Cycles and missing files are errors.
- Directives appear only in `{`$…`}` comments, not in `(*…*)` comments.
- Unknown `{$…}` directives produce a **warning** and are otherwise ignored.
- `--list` does not expand includes.

Included text is spliced into the same compilation unit. Prefer fragments (consts,
helpers) over a second complete `program` inside an include.

---

## Explicitly out of scope

- Units (`unit` / `uses`) and separate compilation
- Pointers / heap, sets, `with`, `goto` / `label`
- Nested record types as fields; enumerated / subrange *types* as a full type system
- Objects, overlays, inline assembler, graphics
- Full directive language (`{$IFDEF}`, `{$R+}`, …) beyond `{$I}` and warn-on-unknown
- Turbo string helpers (`length`, `copy`, `pos`, …); `pack` / `unpack`; `page`

`file` / `text` I/O is planned as [Milestone 8](milestones/08-pascal-runtime-library.md)
Stage 3 after a host-agnostic filesystem façade; it is not supported today.

---

## Checking and examples

```bash
apolloc --check examples/hello.pas
apolloc --emit examples/primes.pas
```

Feature-oriented samples live under [`examples/`](../examples/). When this overview and
the compiler disagree, trust `apolloc --check` / `--emit` and the Pascal tests; update
this document if behaviour is intentional.
