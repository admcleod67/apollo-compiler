# Examples

Sample Pascal programs for Apollo. Each file is self-contained and should pass
`apolloc --check`. Most emit runnable `.tbc` for the standalone Gemini VM.

| File | Features | Expected output (stdout) |
|------|----------|---------------------------|
| [`hello.pas`](hello.pas) | `writeln`, string literal | `Hello, Gemini!` |
| [`count.pas`](count.pas) | `var`, `for` | `1` … `10` (one per line) |
| [`arith.pas`](arith.pas) | `div`, `mod` | `3` then `2` |
| [`reals.pas`](reals.pas) | `real`, integer widening, `/` | π-ish line, `1`, `0.5` |
| [`arrays.pas`](arrays.pas) | `array [lo..hi]`, int/real/string elements | `7`, `1.5`, `ok` |
| [`subprograms.pas`](subprograms.pas) | procedure, function, result assign | `3` then `8` |
| [`primes.pas`](primes.pas) | function, `while`, `mod`, `for` | primes 2..30 |
| [`sieve.pas`](sieve.pas) | `const`, boolean array, nested loops | primes 2..30 |
| [`readsum.pas`](readsum.pas) | `readln`, `writeln` | sum of two typed integers |
| [`ifelse.pas`](ifelse.pas) | `if` … `then` … `else` | `positive` |
| [`while.pas`](while.pas) | `while` | `3`, `2`, `1` |
| [`repeat.pas`](repeat.pas) | `repeat` … `until` | `1`, `2`, `3` |
| [`point.pas`](point.pas) | `record`, field select, whole-record assign | `3` |
| [`case.pas`](case.pas) | `case` … `of` … `else` … `end` | `3` |
| [`include_main.pas`](include_main.pas) | `{$I}` include (`include_helper.inc`) | `included` |
| [`stdfuncs.pas`](stdfuncs.pas) | `ord`/`chr`/`succ`/`pred`/`odd`/`abs`/`sqr`/`trunc`/`round` | `65 A`, then ordinal/arithmetic demos |
| [`stdmath.pas`](stdmath.pas) | `sqrt`/`sin`/`cos`/`arctan`/`ln`/`exp` (needs `gemini-module-math`) | `3`, `0`, `1`, `0`, `0`, `1`, `2` |
| [`filetext.pas`](filetext.pas) | `text`, `assign`/`rewrite`/`reset`/`writeln`/`readln`/`eof` (needs hostfs + basic) | `hello`, `world` |
| [`filetyped.pas`](filetyped.pas) | `file of integer`, `file of` flat record (needs hostfs) | `10`, `20`, `30`, then `3`…`6` |

## Compile and run

```bash
apolloc --check examples/primes.pas
apolloc --emit examples/primes.pas | gemini-vm /dev/stdin
```

File I/O examples need language modules (hostfs; basic for text numeric formatting):

```bash
apolloc --tbc examples/filetext.pas > /tmp/filetext.tbc
gemini-vm --modules "$GEMINI_MODULES_PATH" /tmp/filetext.tbc
```

Interactive example (`readsum.pas`): type two integers at the prompts, then read the sum.

```bash
printf '2\n3\n' | apolloc --emit examples/readsum.pas | gemini-vm /dev/stdin
```

Listing and other driver flags work the same way:

```bash
apolloc --list examples/hello.pas
apolloc --ir examples/arrays.pas
```

Golden bytecode regression tests live under `tests/fixtures/golden/`; several examples
mirror those programs with clearer names for documentation.
