program Subprograms;
procedure greet(n: integer);
begin
  writeln(n);
end;
function twice(n: integer): integer;
begin
  twice := n + n;
end;
begin
  greet(3);
  writeln(twice(4));
end.
