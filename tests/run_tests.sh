#!/bin/bash
cd "$(dirname "$0")/.." || exit 1

clang++ $(llvm-config --cxxflags) src/*.cpp $(llvm-config --ldflags --libs core) -o compiler || exit 1

pass=0
fail=0

for src in tests/ok*.txt; do
  name=${src%.txt}
  rm -f "$name.ll"

  if [ -f "$name.ast" ] && [ "$(./compiler --ast "$src")" != "$(cat "$name.ast")" ]; then
    echo "FAIL $src: --ast differs from $name.ast"
    fail=$((fail + 1))
    continue
  fi
  if ! ./compiler "$src" "$name.ll"; then
    echo "FAIL $src: compiler exited non-zero"
    fail=$((fail + 1))
    continue
  fi

  got=$(lli "$name.ll")
  want=$(cat "$name.expected")
  if [ "$got" = "$want" ]; then
    echo "ok   $src -> $got"
    pass=$((pass + 1))
  else
    echo "FAIL $src"
    echo "     expected: $want"
    echo "     got:      $got"
    fail=$((fail + 1))
  fi
done

for src in tests/bad*.txt; do
  name=${src%.txt}
  rm -f "$name.ll"

  got=$(./compiler "$src" "$name.ll" 2>&1 >/dev/null)
  status=$?
  want=$(cat "$name.expected")

  if [ $status -eq 0 ]; then
    echo "FAIL $src: compiled, but should have failed"
    fail=$((fail + 1))
  elif [ "$got" != "$want" ]; then
    echo "FAIL $src"
    echo "     expected: $want"
    echo "     got:      $got"
    fail=$((fail + 1))
  elif [ -f "$name.ll" ]; then
    echo "FAIL $src: wrote $name.ll despite the error"
    fail=$((fail + 1))
  else
    echo "ok   $src -> $got"
    pass=$((pass + 1))
  fi
done

echo
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
