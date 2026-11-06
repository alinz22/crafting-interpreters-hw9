#!/bin/bash
# Builds every version with the sanitizers on and checks what programs print.
cd "$(dirname "$0")" || exit 1

failed=0
FLAGS="-std=c99 -Wall -Wextra -Wno-unused-parameter -Werror -g -fsanitize=address,undefined"
export ASAN_OPTIONS=abort_on_error=0

build() {
  (cd "$1" && cc $FLAGS $(ls *.c | grep -v -e test_table -e bench_table) -o clox -lm) ||
    { echo "BUILD FAILED $1"; exit 1; }
}

# expect <dir> <expected exit code> <expected output> <program>
expect() {
  local file actual code
  file=$(mktemp)
  printf '%s' "$4" > "$file"
  actual=$("$1/clox" "$file" 2>&1)
  code=$?
  rm -f "$file"
  if [ "$code" != "$2" ] || [ "$actual" != "$3" ]; then
    echo "FAIL [$1] $4"
    echo "  expected ($2): $3"
    echo "  actual   ($code): $actual"
    failed=1
  fi
}

# repl <dir> <expected stdout> <expected stderr> <lines>
repl() {
  local out err errfile
  errfile=$(mktemp)
  out=$(printf '%s\n' "$4" | "$1/clox" 2>"$errfile")
  err=$(cat "$errfile")
  rm -f "$errfile"
  if [ "$out" != "$2" ] || [ "$err" != "$3" ]; then
    echo "FAIL [$1] repl: $4"
    echo "  expected stdout: $2"
    echo "  actual stdout:   $out"
    echo "  expected stderr: $3"
    echo "  actual stderr:   $err"
    failed=1
  fi
}

MIX_ERROR='Operands must be two numbers or two strings.
[line 1] in script'

for dir in ch19/challenge1 ch19/challenge2 ch20; do
  build "$dir"
  expect "$dir" 0 'string' '"st" + "ri" + "ng"'
  expect "$dir" 0 'true' '"string" == "string"'
  expect "$dir" 0 'false' '"string" == "strinG"'
  expect "$dir" 0 'false' '"a" == "ab"'
  expect "$dir" 0 'true' '"st" + "ring" == "string"'
  expect "$dir" 0 'true' '"" == ""'
  expect "$dir" 0 '' '""'
  expect "$dir" 0 '3' '1 + 2'
  expect "$dir" 0 'costs ${5}' '"costs ${5}"'
  expect "$dir" 0 'false' '"1" == 1'
  expect "$dir" 70 "$MIX_ERROR" '"a" + 1'
  expect "$dir" 70 "$MIX_ERROR" '1 + "a"'
  expect "$dir" 70 "$MIX_ERROR" 'nil + "a"'
done

# A constant string has no terminator, so it must not print past its quote.
expect ch19/challenge2 0 'abc' '"abc" == nil ? nil : "abc"'

build ch20
(cd ch20 && make -s test) || failed=1

for dir in ch21/challenge1 ch21/challenge2; do
  build "$dir"
  expect "$dir" 0 '3' 'print 1 + 2;'
  expect "$dir" 0 'beignets with cafe au lait' 'var breakfast = "beignets";
var beverage = "cafe au lait";
breakfast = "beignets with " + beverage;
print breakfast;'
  expect "$dir" 0 'nil' 'var a; print a;'
  expect "$dir" 0 '2' 'var a = 1; var a = 2; print a;'
  expect "$dir" 0 '5
5' 'var a; var b; a = b = 5; print a; print b;'
  expect "$dir" 0 '7' 'var a = 1; print a = 7;'
  expect "$dir" 0 'yes' 'var a = true; print a ? "yes" : "no";'
  expect "$dir" 0 '' '1 + 2; "statement";'
  expect "$dir" 70 "Undefined variable 'nope'.
[line 1] in script" 'print nope;'
  expect "$dir" 70 "Undefined variable 'nope'.
[line 2] in script" 'var a = 1;
nope = 2;'
  expect "$dir" 65 "[line 1] Error at '=': Invalid assignment target." 'var a; var b; var c; var d; a * b = c + d;'
  expect "$dir" 65 "[line 1] Error at '1': Expect variable name.
[line 2] Error at ';': Expect expression." 'var 1 = 2;
print ;'
  # Failing to assign must not leave the variable behind.
  repl "$dir" '> > > ' "Undefined variable 'x'.
[line 1] in script
Undefined variable 'x'.
[line 1] in script" 'x = 1;
print x;'
  # A global stays alive between lines, even though the chunk does not.
  repl "$dir" '> > > hi
> ' "Undefined variable 'y'.
[line 1] in script" 'print y;
var y = "hi";
print y;'
done

# 300 uses of one name fit in a chunk that only holds 256 constants.
many=$(mktemp)
{ echo 'var a = 0;'; for _ in $(seq 300); do echo 'a = a + 1;'; done; echo 'print a;'; } > "$many"
for dir in ch21/challenge1 ch21/challenge2; do
  out=$("$dir/clox" "$many" 2>&1)
  [ "$out" = "300" ] || { echo "FAIL [$dir] repeated name: $out"; failed=1; }
done
rm -f "$many"

# 256 different names is the limit for challenge 1, but not for challenge 2.
names=$(mktemp)
for i in $(seq 0 299); do echo "var v$i = $i;"; done > "$names"
echo 'print v299;' >> "$names"
out=$(ch21/challenge2/clox "$names" 2>&1)
[ "$out" = "299" ] || { echo "FAIL [challenge2] many globals: $out"; failed=1; }
out=$(ch21/challenge1/clox "$names" 2>&1 | head -1)
case "$out" in
  *"Too many constants in one chunk."*) ;;
  *) echo "FAIL [challenge1] limit: $out"; failed=1 ;;
esac
rm -f "$names"

if [ "$failed" = 0 ]; then
  echo "all tests passed"
else
  exit 1
fi
