#! /bin/bash

# see how well leave-one out does with specialized instructions in the
# superinstruction mix

for f in fib sieve random rev copy wc qsort sha1
do
    rm -f tmp
    for g in fib sieve random rev copy wc qsort sha1
    do
	if [ $f != $g ]
	then cat profile.$g >>tmp
	fi
    done
    
    src/gen-combos 200 tmp >src/combos
    make bench
    bin/results.sh "${f}less"
done
