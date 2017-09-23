#! /bin/bash

rm opcode_profile
for f in fib sieve random rev copy wc qsort sha1
do cpp -traditional-cpp benchmarks/$f.idel | ./idelasm | ./profiling-idelvm >/dev/null
done

for f in 0 50 100 150 200 250 300 350 400
do src/gen-combos $f >src/combos
   make bench
   bin/results.sh "${f}*2"
done
