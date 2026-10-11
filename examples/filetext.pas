program FileText;
{ Milestone 8 Stage 3 — text file I/O via Gemini hostfs. }
var
  f: text;
  line: string;
begin
  assign(f, 'filetext_out.txt');
  rewrite(f);
  writeln(f, 'hello');
  writeln(f, 'world');
  close(f);

  reset(f, 'filetext_out.txt');
  while not eof(f) do
  begin
    readln(f, line);
    writeln(line);
  end;
  close(f);
end.
