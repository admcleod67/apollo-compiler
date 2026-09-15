program CaseDemo;

var
  n: integer;

begin
  n := 4;
  case n of
    1: writeln(1);
    2, 3: writeln(2);
    4..6: writeln(3);
  else
    writeln(0)
  end
end.
