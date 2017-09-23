#! /bin/bash
for f in fib sieve random rev copy wc qsort sha1
do 
    cpp -traditional-cpp benchmarks/$f.idel | ./idelasm | ./profiling-idelvm >/dev/null
    mv -i opcode_profile profile.$f
done
