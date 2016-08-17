#!/usr/bin/awk -f

{ count[$1, $2] = $3; lsum[$1] += $3; rsum[$2] += $3; }

END {
  if (totals_by_op) {
    for (i in rsum) {
      printf("%-17s %10u\n",
	     i, rsum[i]);
    }
  } else {
    for (i in count) {
      s = index(i, SUBSEP);
      L = substr(i, 1, s-1);
      R = substr(i, s+1);
      printf("%-17s %-17s %10u %5.1f %5.1f\n", 
	     L, R, count[i], 
	     percent(count[i], rsum[R]),
	     percent(count[i], lsum[L])); 
    }
  }
}

function percent(p, total)
{
  return total == 0 ? 0 : 100 * p/total;
}
