program Primes;
var
  n: integer;

function isPrime(candidate: integer): boolean;
var
  d: integer;
  stillPrime: boolean;
begin
  if candidate < 2 then
    isPrime := false
  else
  begin
    stillPrime := true;
    d := 2;
    while (d * d <= candidate) and stillPrime do
    begin
      if candidate mod d = 0 then
        stillPrime := false;
      d := d + 1;
    end;
    isPrime := stillPrime;
  end;
end;

begin
  for n := 2 to 30 do
    if isPrime(n) then
      writeln(n);
end.
