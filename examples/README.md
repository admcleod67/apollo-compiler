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

## Compile and run

```bash
apolloc --check examples/primes.pas
apolloc --emit examples/primes.pas | gemini-vm /dev/stdin
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
