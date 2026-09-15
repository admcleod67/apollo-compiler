program RecDemo;
type
  point = record x, y: integer; end;
var
  p, q: point;
begin
  p.x := 1;
  q := p;
end.
