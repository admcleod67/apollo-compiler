program Arrays;
var
  a: array [1..3] of integer;
  r: array [-2..2] of real;
  s: array [1..2] of string;
begin
  a[1] := 7;
  writeln(a[1]);
  r[-2] := 1.5;
  writeln(r[-2]);
  s[2] := 'ok';
  writeln(s[2]);
end.
