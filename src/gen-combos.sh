#!/bin/sh
count=$1
shift

awk '
{
  count = $1;
  for (i = 3; i < NF; ++i)
    pair[$i,$(i+1)] += count;
}

END {
  for (p in pair) {
    q = p;
    sub(SUBSEP, " ", q);
    print q, pair[p];
  }
}
' "$@" | 

awk -f src/profile.awk | 
sort -rn +2 | 
awk '$1 !~ /CALL|RETURN|JUMP|BRANCH|^GRAB$/ && $2 !~ /^GRAB$/ {
  print $1 "\t" $2 
}' | 
head -"$count"
