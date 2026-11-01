#!/bin/bash
cd "$(dirname "$0")/.." || exit 1

flags=()
for arg in "$@"; do
  case $arg in
    --remove-unused) flags+=(--remove-unused) ;;
    *) echo "usage: $0 [--remove-unused]"; exit 1 ;;
  esac
done

clang++ $(llvm-config --cxxflags) src/*.cpp $(llvm-config --ldflags --libs core) -o compiler || exit 1

pass=0
fail=0

for src in tests/ok/*.txt; do
  name=${src%.txt}
  rm -f "$name.ll"

  if [ -f "$name.ast" ] && [ "$(./compiler --ast "$src")" != "$(cat "$name.ast")" ]; then
    echo "FAIL $src: --ast differs from $name.ast"
    fail=$((fail + 1))
    continue
  fi
  if [ -f "$name.ast-checked" ] && [ "$(./compiler --ast-checked "$src")" != "$(cat "$name.ast-checked")" ]; then
    echo "FAIL $src: --ast-checked differs from $name.ast-checked"
    fail=$((fail + 1))
    continue
  fi
  if ! ./compiler "${flags[@]}" "$src" "$name.ll"; then
    echo "FAIL $src: compiler exited non-zero"
    fail=$((fail + 1))
    continue
  fi

  input=/dev/null
  [ -f "$name.in" ] && input="$name.in"
  got=$(lli "$name.ll" < "$input")
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

for src in tests/opt/*.txt; do
  name=${src%.txt}
  input=/dev/null
  [ -f "$name.in" ] && input="$name.in"
  want=$(cat "$name.expected")

  result=ok
  if [ "$(./compiler --remove-unused --ast-checked "$src")" != "$(cat "$name.astc")" ]; then
    result="--remove-unused --ast-checked differs from $name.astc"
  fi
  for flag in "" --remove-unused; do
    [ "$result" = ok ] || break
    how=${flag:+with $flag}
    how=${how:-without --remove-unused}
    rm -f "$name.ll"
    if ! ./compiler $flag "$src" "$name.ll"; then
      result="compiler exited non-zero $how"
    elif [ "$(lli "$name.ll" < "$input")" != "$want" ]; then
      result="output $how differs from $name.expected"
    fi
  done

  if [ "$result" = ok ]; then
    echo "ok   $src -> $want"
    pass=$((pass + 1))
  else
    echo "FAIL $src: $result"
    fail=$((fail + 1))
  fi
done

for src in tests/err/*.txt; do
  name=${src%.txt}
  rm -f "$name.ll"

  got=$(./compiler "${flags[@]}" "$src" "$name.ll" 2>&1 >/dev/null)
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
