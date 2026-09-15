program RepeatDemo;
var
  n: integer;
begin
  n := 1;
  repeat
    writeln(n);
    n := n + 1;
  until n > 3;
end.
