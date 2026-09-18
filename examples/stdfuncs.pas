program StdFuncs;
{ Milestone 8 Stage 1 — ordinal and arithmetic standard functions. }
var
  i: integer;
  r: real;
  c: char;
  b: boolean;
begin
  i := ord('A');
  c := chr(65);
  writeln(i, ' ', c);

  i := succ(10);
  i := pred(i);
  b := odd(i);
  if b then
    writeln(i, ' odd')
  else
    writeln(i, ' even');

  i := abs(-7);
  r := abs(-2.5);
  writeln(i, ' ', r);

  i := sqr(4);
  r := sqr(1.5);
  writeln(i, ' ', r);

  i := trunc(3.9);
  writeln(i);
  i := trunc(-3.9);
  writeln(i);

  i := round(1.5);
  writeln(i);
  i := round(-1.5);
  writeln(i);
end.
