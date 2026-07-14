# Examples

Sample Apollo source programs live here. The first language front-end is **Pascal**.

| File | Notes |
|------|--------|
| [`hello.pas`](hello.pas) | Console `writeln` greeting |
| [`count.pas`](count.pas) | `var` + `for` loop printing 1..10 |

```bash
apolloc --list examples/hello.pas
apolloc --tokens examples/count.pas
```

Later milestones will compile these examples to Gemini VM bytecode (`.tbc`).
