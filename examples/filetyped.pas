program FileTyped;
{ Milestone 8 Stage 3 — typed file of integer and flat record. }
type
  Point = record
    x: integer;
    y: integer;
  end;
var
  fi: file of integer;
  fr: file of Point;
  n: integer;
  p: Point;
begin
  rewrite(fi, 'filetyped_int.bin');
  write(fi, 10);
  write(fi, 20);
  write(fi, 30);
  close(fi);

  reset(fi, 'filetyped_int.bin');
  while not eof(fi) do
  begin
    read(fi, n);
    if not eof(fi) then
      writeln(n);
  end;
  close(fi);

  p.x := 3;
  p.y := 4;
  rewrite(fr, 'filetyped_rec.bin');
  write(fr, p);
  p.x := 5;
  p.y := 6;
  write(fr, p);
  close(fr);

  reset(fr);
  read(fr, p);
  writeln(p.x);
  writeln(p.y);
  read(fr, p);
  writeln(p.x);
  writeln(p.y);
  close(fr);
end.
