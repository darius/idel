a := benchmarks

$a-benchmark: $a/big-input $a/copy.ok
	$a/runbench random
	$a/runbench copy <$a/big-input
	for i in 0 1 2 3 4 5 6 7 8 9; do for j in 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9; do cat COPYING; done; done | $a/runbench wc
	$a/runbench qsort
	$a/runbench bmh <COPYING
	$a/runbench sha1

$a-quickbench: $a/bigload.image
	$a/runbench fib
	$a/runbench sieve
	date >>$a/bigload.times
	wc $a/bigload.image >>$a/bigload.times
	(/usr/bin/time -p bin/idelvm $a/bigload.image) 2>>$a/bigload.times
	tail -3 <$a/bigload.times

oldbench:
	$a/runbench hotpo
	$a/runbench tak
	$a/runbench rev


$a/bigload.idel: $a/gen.awk
	$(AWK) -f $< >$@

$a/bigload.image: $a/bigload.idel bin/idelasm
	bin/idelasm $a/bigload.idel >$@

$a/big-input: $a/input-file.gz
	gzip -dc $< >$@

$a/copy.ok: $a/big-input
	ln $< $@


CLEAN += $a/big-input $a/copy.ok $a/image $a/output \
         $a/bigload.idel $a/bigload.image
BENCHMARKS += $a-benchmark
QUICKBENCH += $a-quickbench
