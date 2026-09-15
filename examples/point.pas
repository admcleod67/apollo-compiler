program PointDemo;

type
  Point = record
    x, y: integer;
  end;

var
  p, q: Point;

begin
  p.x := 1;
  p.y := 2;
  q := p;
  writeln(p.x + q.y)
end.
