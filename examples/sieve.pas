program Sieve;
const
  limit = 30;
var
  composite: array [2..limit] of boolean;
  i, j: integer;
begin
  for i := 2 to limit do
    composite[i] := false;
  for i := 2 to limit do
    if not composite[i] then
    begin
      writeln(i);
      j := i + i;
      while j <= limit do
      begin
        composite[j] := true;
        j := j + i;
      end;
    end;
end.
