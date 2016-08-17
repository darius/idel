#!/bin/sh

awk '
BEGIN { p = 1; c = 0; printf("%-8s ", "'"$1"'"); }

FNR == 1 { flush(); }

$1 == "user" { t = $2; }

END { flush(); printf("%6.2f\n", p^(1/c)); }

function flush()
{
  if (t) {
    printf("%8.2f", t);
    p *= t; ++c;
  }
}
' benchmarks/{fib,sieve,random,rev,copy,wc,qsort,sha1}.times >>statistics
