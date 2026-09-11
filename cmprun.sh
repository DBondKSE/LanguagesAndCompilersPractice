set -eu
./compiler $1 output.ll
llc -filetype=obj -relocation-model=pic output.ll -o tmp_cmp.o
clang -fPIE tmp_cmp.o -o program
./program
rm tmp_cmp.o
