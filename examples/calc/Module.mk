d := examples/calc

examples-calc-test: $d/calc
	$d/test-me

$d/calc: $d/calc.idel bin/idelasm
	cpp $< | bin/idelasm >$@


EXAMPLES += $d/calc
TESTS += examples-calc-test
CLEAN += $d/calc tmp-output
